#include <QDebug>
#include <QPaintEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>

// Just for qtwebengine...
#include <QtWebEngine/qtwebengineglobal.h>

#include "qmlview.h"

static bool m_weinit {false};

OBSQuickview::OBSQuickview(QObject *parent)
    : QObject(parent)
{
    texture = NULL;
    m_bits = NULL;
    m_snapper.clear();
    m_ready = false;
    m_updated = false;

    if( !m_weinit )
    {
        m_weinit = true;
        QtWebEngine::initialize();
    }

    connect( this, SIGNAL(wantLoad(QUrl)), this, SLOT(doLoad(QUrl)), Qt::QueuedConnection );
    connect( this, SIGNAL(wantResize(quint32,quint32)), this, SLOT(doResize(quint32,quint32)), Qt::QueuedConnection );
    connect( this, SIGNAL(wantSnap()), this, SLOT(doSnap()), Qt::QueuedConnection );

    obs_enter_graphics();
    m_quick = new QQuickWidget();
/*
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    format.setSwapBehavior(QSurfaceFormat::TripleBuffer);
    m_quick->setFormat(format);
*/
    connect( m_quick, SIGNAL(statusChanged(QQuickWidget::Status)), this, SLOT(qmlStatus(QQuickWidget::Status)) );

    m_scene = new QGraphicsScene();
    //connect( m_scene, SIGNAL(changed(QList<QRectF>)), this, SLOT(doSnap()), Qt::QueuedConnection );

    m_view = new QGraphicsView(m_scene);

    m_view->setStyleSheet("{ background-color: rgba(0,0,0,0); }");
    m_scene->addWidget(m_quick);
    m_quick->setStyleSheet("{ background-color: rgba(0,0,0,0); }");
    m_quick->setAutoFillBackground(false);

    m_quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quick->show();
    obs_leave_graphics();
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

    m_quick->deleteLater();
    m_view->deleteLater();
    m_scene->deleteLater();
}

void OBSQuickview::snap()
{
    emit wantSnap();
}

void OBSQuickview::doSnap()
{
    if( !m_quick || !m_quick->rootObject() || !m_ready )
        return;

    if( !m_snapper.isNull() )
        return;

    if( m_quick->width() < 5 || m_quick->height() < 5 )
        return;

    m_mutex.lock();
    obs_enter_graphics();
    m_snapper = m_quick->rootObject()->grabToImage();
    obs_leave_graphics();
    m_mutex.unlock();

    if( !m_snapper.isNull() )
        connect( m_snapper.data(), SIGNAL(ready()), this, SLOT(qmlFrame()), Qt::QueuedConnection );
}

void OBSQuickview::makeTexture()
{
    obs_enter_graphics();
    if( texture )
    {
        gs_texture_destroy( texture );
        delete[] m_bits;
    }

    m_bits = new quint8[ 4 * m_quick->width() * m_quick->height() ];
    m_canvas = QImage( m_quick->width(), m_quick->height(), QImage::Format_RGBA8888 );
    texture = gs_texture_create( m_quick->width(), m_quick->height(), GS_RGBA, 1, (const uint8_t **)&m_bits, GS_DYNAMIC );
    obs_leave_graphics();
    //qDebug() << "makeTexture(" << m_web->width() << "x" << m_web->height() << ")";
}

void OBSQuickview::loadUrl(QUrl url)
{
    m_ready = false;
    emit wantLoad(url);
}

void OBSQuickview::doLoad(QUrl url)
{
    qDebug() << "Loading URL: " << url;
    m_quick->setSource(url);
}

void OBSQuickview::resize( quint32 w, quint32 h )
{
    emit wantResize(w, h);
}

void OBSQuickview::doResize(quint32 w, quint32 h)
{
qDebug() << "Resizing: " << w << "x" << h;
    m_quick->resize(w, h);
    m_scene->setSceneRect(0, 0, w, h);
    m_view->resize(w, h);
    m_view->ensureVisible(0, 0, w, h, 0, 0);
}

void OBSQuickview::qmlFrame()
{
    m_mutex.lock();
    m_canvas = m_snapper.data()->image().convertToFormat(QImage::Format_RGBA8888);
    m_mutex.unlock();
    m_snapper.clear();
    m_updated = true;
}

void OBSQuickview::obsdraw()
{
    snap();

    if( !m_updated )
        return; // Frame hasn't changed.

    if( m_canvas.isNull() )
    {
        qDebug() << "Null grab.";
        return;
    }

    qint32 sw = obs_source_get_width(source);
    qint32 sh = obs_source_get_height(source);
    QImage img;
    if( sw != m_quick->width() || sh != m_quick->height() || !texture )
    {
        qDebug() << "Rescaling to " << sw << "x" << sh;
        m_mutex.lock();
        img = m_canvas.scaled( sw, sh );
        m_mutex.unlock();

        //m_quick->resize( sw, sh );
        makeTexture();
    }
    else
    {
        //qDebug() << "Not rescaling...";
        m_mutex.lock();
        img = m_canvas.convertToFormat(QImage::Format_RGBA8888);
        m_mutex.unlock();
    }

    m_updated = false;

    obs_enter_graphics();
    gs_texture_set_image(texture, img.constBits(), 4 * img.width(), false);
    obs_leave_graphics();
}

void OBSQuickview::renderFrame(gs_effect_t *effect)
{
    if( !texture ) return;

    gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture);
    gs_draw_sprite(texture, 0, m_canvas.width(), m_canvas.height());
}

void OBSQuickview::qmlStatus(QQuickWidget::Status status)
{
    switch( status )
    {
        case QQuickView::Null:
            qDebug() << "obs-quickview: Null";
            m_ready = false;
            break;
        case QQuickView::Ready:
            qDebug() << "obs-quickview: Ready";
            m_ready = true;
            break;
        case QQuickView::Loading:
            qDebug() << "obs-quickview: Loading";
            m_ready = false;
            break;
        case QQuickView::Error:
            qDebug() << "obs-quickview: Error";
            m_ready = false;
            foreach( QQmlError err, m_quick->errors() )
            {
                qDebug() << " * " << err.toString();
            }

            break;
        default:
            qDebug() << "obs-quickview: UNKNOWN";
            break;
    }
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
    quint32 w = obs_data_get_int(settings, "width");
    quint32 h = obs_data_get_int(settings, "height");

    if( w > 10 && h > 10 )
        s->resize( w, h );

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
    s->m_persistent = !unload;
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
    OBSQuickview *s = (OBSQuickview *)data;
    s->deleteLater();
}

static void quickview_source_defaults(obs_data_t *settings)
{
    obs_data_set_default_bool(settings, "unload", false);
}

static void quickview_source_show(void *data)
{
    OBSQuickview *s = (OBSQuickview *)data;
    s->obsshow();
}

static void quickview_source_hide(void *data)
{
    OBSQuickview *s = (OBSQuickview *)data;
    s->obshide();
}

static uint32_t quickview_source_getwidth(void *data)
{
    OBSQuickview *s = (OBSQuickview *)data;
    return s->width();
}

static uint32_t quickview_source_getheight(void *data)
{
    OBSQuickview *s = (OBSQuickview *)data;
    return s->height();
}

static void quickview_source_render(void *data, gs_effect_t *effect)
{
    OBSQuickview *s = (OBSQuickview *)data;
    s->renderFrame(effect);
}

static void quickview_source_tick(void *data, gs_effect_t *effect)
{
    OBSQuickview *s = (OBSQuickview *)data;
    s->renderFrame(effect);
}

static void quickview_source_tick(void *data, float seconds)
{
    OBSQuickview *s = (OBSQuickview *)data;
    Q_UNUSED(seconds);

    if( obs_source_active(s->source) )
        s->obsdraw();
}

static obs_properties_t *quickview_source_properties(void *data)
{
    Q_UNUSED(data);

    obs_properties_t *props = obs_properties_create();
    obs_properties_add_text(props, "file", obs_module_text("URL"), OBS_TEXT_DEFAULT);
    obs_properties_add_bool(props, "unload", obs_module_text("UnloadWhenNotShowing"));
    obs_properties_add_int(props, "width", obs_module_text("Width"), 1, 4096, 1);
    obs_properties_add_int(props, "height", obs_module_text("Height"), 1, 4096, 1);

    return props;
}

static struct obs_source_info quickview_source_info;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("quickview-source", "en-US")

bool obs_module_load(void)
{
    quickview_source_info.id             = "quickview_source";
    quickview_source_info.type           = OBS_SOURCE_TYPE_INPUT;
    quickview_source_info.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC;
    quickview_source_info.get_name       = quickview_source_get_name;
    quickview_source_info.create         = quickview_source_create;
    quickview_source_info.destroy        = quickview_source_destroy;
    quickview_source_info.update         = quickview_source_update;
    quickview_source_info.get_defaults   = quickview_source_defaults;
    quickview_source_info.show           = quickview_source_show;
    quickview_source_info.hide           = quickview_source_hide;
    quickview_source_info.get_width      = quickview_source_getwidth;
    quickview_source_info.get_height     = quickview_source_getheight;
    quickview_source_info.video_render   = quickview_source_render;
    quickview_source_info.video_tick     = quickview_source_tick;
    quickview_source_info.get_properties = quickview_source_properties;

    obs_register_source(&quickview_source_info);
    return true;
}
