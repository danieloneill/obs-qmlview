#include <QDebug>
#include <QPaintEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>

#include "qmlview.h"
#include "renderer.h"

#include <QtWebEngine/qtwebengineglobal.h>

#include <sys/time.h>

static bool m_weinit {false};

void OBSQuickview::addPluginsPath()
{
    // FIXME: ... so kludge. wow.
    QDir d = QDir(QCoreApplication::applicationDirPath());
    QString bitpart = d.dirName();
    QString newPath = QDir::currentPath() + "/../../obs-plugins/" + bitpart + "/obs-qmlview/plugins";
    m_messages << "Adding to library path: " << newPath;
    QCoreApplication::addLibraryPath( newPath );
}

OBSQuickview::OBSQuickview(QObject *parent)
    : QObject(parent)
{
    texture = NULL;
    m_bits = NULL;
    m_quickView = NULL;
    m_ready = false;
    m_sceneRendered = false;
    m_frameLimited = false;
    m_nextFrame = 0;
    m_enabled = false;
    m_canvas = QImage();

    if( !m_weinit )
    {
        m_weinit = true;
        QtWebEngine::initialize();
        addPluginsPath();
    }

    connect( this, SIGNAL(wantLoad()), this, SLOT(doLoad()), Qt::QueuedConnection );
    connect( this, SIGNAL(wantUnload()), this, SLOT(doUnload()), Qt::QueuedConnection );
    connect( this, SIGNAL(wantResize(quint32,quint32)), this, SLOT(doResize(quint32,quint32)), Qt::QueuedConnection );

    m_quickView = new WindowSingleThreaded();
    m_quickView->addMessages(m_messages);

    connect( m_quickView, &WindowSingleThreaded::capped, this, &OBSQuickview::qmlCopy );

    m_renderCounter = new FrameCounter("QmlView::render");
    m_drawCounter = new FrameCounter("QmlView::draw");
    m_qmlFrameCounter = new FrameCounter("QmlView::frame");

    connect( this, &OBSQuickview::frameRendered, this, &OBSQuickview::qmlFrame, Qt::QueuedConnection );
}

OBSQuickview::~OBSQuickview()
{
    m_mutex.lock();
    if( texture )
    {
        gs_texture_destroy( texture );
        delete[] m_bits;
    }
    texture = NULL;
    m_bits = NULL;
    m_mutex.unlock();

    delete m_quickView;

    m_renderCounter->deleteLater();
    m_drawCounter->deleteLater();
    m_qmlFrameCounter->deleteLater();
}

void OBSQuickview::makeTexture()
{
    obs_enter_graphics();
    if( texture )
    {
        gs_texture_destroy( texture );
        delete[] m_bits;
    }

    quint32 w = m_size.width();
    quint32 h = m_size.height();
    m_bits = new quint8[ 4 * w * h ];
    m_canvas = QImage( w, h, QImage::Format_RGBA8888 );
    texture = gs_texture_create( w, h, GS_RGBA, 1, (const uint8_t **)&m_bits, GS_DYNAMIC );
    obs_leave_graphics();
    qDebug() << "makeTexture(" << w << "x" << h << ")";
}

void OBSQuickview::loadUrl(QUrl url)
{
    m_ready = false;
    m_source = url;
    if( m_enabled )
        emit wantLoad();
}

void OBSQuickview::doLoad()
{
    qDebug() << "Loading URL: " << m_source;
    m_quickView->startQuick(m_source);
}

void OBSQuickview::doUnload()
{
    qDebug() << "Unloading URL: " << m_source;
    m_quickView->unload();
}

void OBSQuickview::resize( quint32 w, quint32 h )
{
    m_size = QSize(w, h);
    qDebug() << "Resize: " << m_size;
    emit wantResize(w, h);
}

void OBSQuickview::doResize(quint32 w, quint32 h)
{
    if( m_quickView )
        m_quickView->resize(QSize(w,h));
}

void OBSQuickview::obsshow()
{
    m_enabled = true;
    //emit startForced(1000/m_fps);
    if(!m_persistent || !m_quickView->initialised())
        emit wantLoad();
}

void OBSQuickview::obshide()
{
    m_enabled = false;
    if(!m_persistent)
        emit wantUnload();
}

void OBSQuickview::qmlCheckFrame()
{
    // Okay, ready for the next frame.
    m_drawCounter->inc();
    /*
    if( !m_sceneRendered )
        return;
*/
    // Renderer has shown the last frame, so now we want a new one.
    m_qmlFrameCounter->inc();
    m_quickView->wantFrame();
}

void OBSQuickview::qmlFrame()
{
    if( frameDue() )
    {
        qmlCheckFrame();
        frameSynced();
    }
}

void OBSQuickview::qmlCopy()
{
    m_sceneRendered = false;

#ifndef GLCOPY
    //m_mutex.lock();
    m_quickView->lock();
    m_canvas = QImage(*m_quickView->getImage());
    m_quickView->unlock();
    //m_mutex.unlock();
#else
    m_texid = texid;
#endif

    if( !texture )
    {
        m_size = m_canvas.size();
        makeTexture();
    }

    obsdraw();
}

bool OBSQuickview::obsdraw()
{
    if( !obs_source_active(source) )
    {
        qDebug() << "Scene isn't active.";
        return false;
    }

    quint32 sw = obs_source_get_width(source);
    quint32 sh = obs_source_get_height(source);
    //qDebug() << "Source: " << sw << "x" << sh;
    if( sw <= 0 || sh <= 0 )
    {
        qDebug() << "Invalid source dimensions.";
        return false;
    }

#ifndef GLCOPY
    if( m_canvas.isNull() )
    {
        qDebug() << "Null grab.";
        return false;
    }
/*
    QImage img;
    if( sw != (quint32)m_canvas.width() || sh != (quint32)m_canvas.height() )
    {
        qDebug() << "Rescaling to " << sw << "x" << sh;
        m_mutex.lock();
        img = m_canvas.scaled( sw, sh );
        m_mutex.unlock();
    }
    else
    {
        //qDebug() << "Not rescaling...";
        m_mutex.lock();
        img = m_canvas;
        m_mutex.unlock();
    }
*/
#endif

    quint32 texw=0, texh=0;
    if( texture )
    {
        obs_enter_graphics();
        texw = gs_texture_get_width(texture);
        texh = gs_texture_get_height(texture);
        obs_leave_graphics();
    }

    if( !texture || sw != texw || sh != texh )
    {
        m_size = m_canvas.size();
        if( texture )
            qDebug() << sw  << "!=" << texw << "||" << sh << "!=" << texh;
        makeTexture();
    }

    obs_enter_graphics();
#ifndef GLCOPY
    gs_texture_set_image(texture, m_canvas.constBits(), 4 * m_canvas.width(), false);
    //gs_texture_set_image(texture, img.constBits(), 4 * img.width(), false);
#else
    m_texid = m_quickView->lockFbo();
    gs_load_texture(texture, m_texid);
    m_quickView->unlockFbo();
#endif
    obs_leave_graphics();

    return true;
}

void OBSQuickview::renderFrame(gs_effect_t *effect)
{
    m_sceneRendered = true;
    if( !texture ) {
        //qDebug() << "!texture";
        return;
    }

    gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture);
    //while (gs_effect_loop(obs_get_base_effect(OBS_EFFECT_PREMULTIPLIED_ALPHA), "Draw"))
        obs_source_draw(texture, 0, 0, 0, 0, false);

    //gs_draw_sprite(texture, 0, gs_texture_get_width(texture), gs_texture_get_height(texture));

    m_renderCounter->inc();

    emit frameRendered();
    qmlFrame();
}

void OBSQuickview::renderFrameCustom(gs_effect_t *effect)
{
    if( !texture ) return;
    while (gs_effect_loop(obs_get_base_effect(OBS_EFFECT_PREMULTIPLIED_ALPHA), "Draw"))
        obs_source_draw(texture, 0, 0, 0, 0, false);

    m_sceneRendered = true;
    m_renderCounter->inc();
}

bool OBSQuickview::frameDue()
{
    if( !m_frameLimited )
        return true;

    // frame time in seconds: (usually 0.xxxxxxx)
    double fdiff = 1.0/(double)m_fps;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    double thisFrame = (double)tv.tv_sec + ((double)tv.tv_usec * 0.000001);

    if( m_nextFrame <= 0 )
    {
        m_nextFrame = thisFrame + fdiff;
        return true;
    }

    //qDebug() << "thisFrame:" << thisFrame << " / m_lastFrame:" << m_lastFrame << " / fdiff:" << fdiff << " / m_fps:" << m_fps;
    if( thisFrame >= m_nextFrame )
        return true;

    return false;
}

//quint32 syncLoop = 0;
void OBSQuickview::frameSynced()
{
    //syncLoop++;
    double fdiff = 1.0/(double)m_fps;

    if( m_nextFrame == 0 )
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        m_nextFrame = (double)tv.tv_sec + ((double)tv.tv_usec * 0.000001) + fdiff;
    }
    else
    {
        m_nextFrame += fdiff;
    }
}

void OBSQuickview::tick(quint64 seconds)
{
    return;
}

static const char *quickview_source_get_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("quickview");
}

static void quickview_source_update(void *data, obs_data_t *settings)
{
    OBSQuickview *s = (OBSQuickview *)data;
    const char *file = obs_data_get_string(settings, "file");
    const bool unload = obs_data_get_bool(settings, "unload");
    const bool force = obs_data_get_bool(settings, "force");
    quint32 w = obs_data_get_int(settings, "width");
    quint32 h = obs_data_get_int(settings, "height");
    quint32 fps = obs_data_get_int(settings, "fps");
    s->m_fps = fps;
    s->m_frameLimited = (fps > 0);

    s->m_quickView->manualUpdated( fps > 0 );
    // s->m_quickView->manualUpdated(force);

    if( w > 10 && h > 10 )
        s->resize( w, h );

    s->m_persistent = !unload;
    QString str( QString::fromLocal8Bit(file) );
    QUrl url(str);
    if( url.isValid() && !url.isEmpty() )
    {
        if( url.isLocalFile() )
        {
            QFileInfo fi( url.toLocalFile() );
            if( fi.exists() )
                s->loadUrl( url );
        }
        else
            s->loadUrl( url );
    }
}

static void *quickview_source_create(obs_data_t *settings, obs_source_t *source)
{
    OBSQuickview *context = new OBSQuickview();
    context->source = source;

    quickview_source_update(context, settings);
    return context;
}

static void quickview_source_destroy(void *data)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;
    s->deleteLater();
}

static void quickview_source_defaults(obs_data_t *settings)
{
    obs_data_set_default_bool(settings, "unload", false);
}

static void quickview_source_show(void *data)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;
    s->obsshow();
}

static void quickview_source_hide(void *data)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;
    s->obshide();
}

static uint32_t quickview_source_getwidth(void *data)
{
    if(!data) return 5;
    OBSQuickview *s = (OBSQuickview *)data;
    return s->width();
}

static uint32_t quickview_source_getheight(void *data)
{
    if(!data) return 5;
    OBSQuickview *s = (OBSQuickview *)data;
    return s->height();
}

static void quickview_source_render(void *data, gs_effect_t *effect)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;
    s->renderFrame(effect);
}

static void quickview_source_render_custom(void *data, gs_effect_t *effect)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;
    s->renderFrameCustom(effect);
}

static void quickview_source_tick(void *data, float seconds)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;
    s->tick(seconds);
/*
    if( obs_source_active(s->source) )
        s->obsdraw(seconds);
        */
}

static obs_properties_t *quickview_source_properties(void *data)
{
    Q_UNUSED(data);

    obs_properties_t *props = obs_properties_create();
    obs_properties_add_text(props, "file", obs_module_text("URL (eg: file:///C:/scenes/main.qml)"), OBS_TEXT_DEFAULT);
    obs_properties_add_bool(props, "unload", obs_module_text("Reload when made visible"));
    obs_properties_add_bool(props, "force", obs_module_text("Force rendering"));
    obs_properties_add_int(props, "fps", obs_module_text("Limited FPS (0 for unlimited"), 0, 60, 1);
    obs_properties_add_int(props, "width", obs_module_text("Width"), 1280, 4096, 1);
    obs_properties_add_int(props, "height", obs_module_text("Height"), 720, 4096, 1);

    return props;
}

static void quickview_source_mouse_click(void *data,
                const struct obs_mouse_event *event, int32_t type,
                bool mouse_up, uint32_t click_count)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;

    //qDebug() << "Click: " << event->x << "x" << event->y << " -> " << mouse_up << " => " << click_count;
    s->m_quickView->sendMouseClick( event->x, event->y, type, mouse_up, click_count );
}

static void quickview_source_mouse_move(void *data,
                const struct obs_mouse_event *event, bool mouse_leave)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;

    //qDebug() << "Move: " << event->x << "x" << event->y << " -> " << mouse_leave;
    s->m_quickView->sendMouseMove( event->x, event->y, mouse_leave );
}

static void quickview_source_mouse_wheel(void *data,
                const struct obs_mouse_event *event, int x_delta, int y_delta)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;
    s->m_quickView->sendMouseWheel(x_delta, y_delta);
}

static void quickview_source_focus(void *data, bool focus)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;
    s->m_quickView->sendFocus(focus);
}

static void quickview_source_key_click(void *data,
                const struct obs_key_event *event, bool key_up)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;
    s->m_quickView->sendKey(event->native_scancode, event->native_vkey, event->native_modifiers, event->text, key_up);
}

static struct obs_source_info quickview_source_info;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("quickview-source", "en-US")

bool obs_module_load(void)
{
    quickview_source_info.id             = "quickview_source";
    quickview_source_info.type           = OBS_SOURCE_TYPE_INPUT;
    quickview_source_info.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_INTERACTION | OBS_SOURCE_DO_NOT_DUPLICATE;
    //quickview_source_info.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_INTERACTION | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CUSTOM_DRAW;

    quickview_source_info.mouse_click   = quickview_source_mouse_click;
    quickview_source_info.mouse_move    = quickview_source_mouse_move;
    quickview_source_info.mouse_wheel   = quickview_source_mouse_wheel;
    quickview_source_info.focus         = quickview_source_focus;
    quickview_source_info.key_click     = quickview_source_key_click;

    quickview_source_info.get_name       = quickview_source_get_name;
    quickview_source_info.create         = quickview_source_create;
    quickview_source_info.destroy        = quickview_source_destroy;
    quickview_source_info.update         = quickview_source_update;
    quickview_source_info.get_defaults   = quickview_source_defaults;
    quickview_source_info.show           = quickview_source_show;
    quickview_source_info.hide           = quickview_source_hide;
    quickview_source_info.get_width      = quickview_source_getwidth;
    quickview_source_info.get_height     = quickview_source_getheight;
    //quickview_source_info.video_render   = quickview_source_render_custom;
    quickview_source_info.video_render   = quickview_source_render;
    //quickview_source_info.video_tick     = quickview_source_tick;
    quickview_source_info.get_properties = quickview_source_properties;

    obs_register_source(&quickview_source_info);
    return true;
}
