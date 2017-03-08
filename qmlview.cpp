#include <QDebug>
#include <QPaintEvent>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>

#include "qmlview.h"
#include "renderer.h"

static bool m_weinit {false};

void OBSQuickview::addPluginsPath()
{
    // FIXME: ... so kludge. wow.
    QDir d = QDir::current();
    QString bitpart = d.dirName();
    QString newPath = QDir::currentPath() + "/../../obs-plugins/" + bitpart + "/obs-qmlview";
    qDebug() << "Adding to library path: " << newPath;
    QCoreApplication::addLibraryPath( newPath );
}

void OBSQuickview::addQmlPath()
{
    // FIXME: ... so kludge. wow.
    QDir d = QDir::current();
    QString bitpart = d.dirName();

    QString newPath = QDir::currentPath() + "/../../obs-plugins/" + bitpart + "/obs-qmlview/qml";
    qDebug() << "Adding to imports path: " << newPath;
    m_quickView->engine()->addImportPath(newPath);
    foreach( QString path, m_quickView->engine()->importPathList() )
        qDebug() << " >>  Import: " << path;

    newPath = QDir::currentPath() + "/../../obs-plugins/" + bitpart + "/obs-qmlview/plugins";
    qDebug() << "Adding to plugins path: " << newPath;
    m_quickView->engine()->addPluginPath(newPath);
    foreach( QString path, m_quickView->engine()->pluginPathList() )
        qDebug() << " >>  Plugin: " << path;
}

OBSQuickview::OBSQuickview(QObject *parent)
    : QObject(parent)
{
    texture = NULL;
    m_bits = NULL;
    m_quickView = NULL;
    m_ready = false;
    m_updated = false;
    m_enabled = false;

    if( !m_weinit )
    {
        m_weinit = true;
        addPluginsPath();
    }

    connect( this, SIGNAL(wantLoad()), this, SLOT(doLoad()), Qt::QueuedConnection );
    connect( this, SIGNAL(wantUnload()), this, SLOT(doUnload()), Qt::QueuedConnection );
    connect( this, SIGNAL(wantResize(quint32,quint32)), this, SLOT(doResize(quint32,quint32)), Qt::QueuedConnection );
    connect( this, SIGNAL(wantSnap()), this, SLOT(doSnap()), Qt::QueuedConnection );

    m_quickView = new WindowSingleThreaded();
    connect( m_quickView, &WindowSingleThreaded::updated, this, &OBSQuickview::qmlFrame );
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
}

void OBSQuickview::snap()
{
    emit wantSnap();
}

void OBSQuickview::doSnap()
{
    m_quickView->render();
}

void OBSQuickview::makeTexture()
{
    obs_enter_graphics();
    if( texture )
    {
        gs_texture_destroy( texture );
        delete[] m_bits;
    }

    m_bits = new quint8[ 4 * m_canvas.width() * m_canvas.height() ];
    m_canvas = QImage( m_canvas.width(), m_canvas.height(), QImage::Format_RGBA8888 );
    texture = gs_texture_create( m_canvas.width(), m_canvas.height(), GS_RGBA, 1, (const uint8_t **)&m_bits, GS_DYNAMIC );
    obs_leave_graphics();
    //qDebug() << "makeTexture(" << m_web->width() << "x" << m_web->height() << ")";
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
    if(!m_persistent || !m_quickView->initialised())
        emit wantLoad();
}

void OBSQuickview::obshide()
{
    m_enabled = false;
    if(!m_persistent)
        emit wantUnload();
}

void OBSQuickview::qmlFrame(GLuint texid)
{
    m_mutex.lock();
    m_canvas = m_quickView->getImage().convertToFormat(QImage::Format_RGBA8888);
    m_mutex.unlock();

    m_texid = texid;
    m_updated = true;
}

void OBSQuickview::obsdraw()
{
    snap();

    if( !m_updated )
        return; // Frame hasn't changed.

    qint32 sw = obs_source_get_width(source);
    qint32 sh = obs_source_get_height(source);

    if( m_canvas.isNull() )
    {
        qDebug() << "Null grab.";
        return;
    }

    QImage img;
    if( sw != m_canvas.width() || sh != m_canvas.height() )
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

    if( !texture || sw != gs_texture_get_width(texture) || sh != gs_texture_get_height(texture) )
        makeTexture();

    m_updated = false;

    obs_enter_graphics();
    //gs_load_texture(texture, m_texid);
    gs_texture_set_image(texture, img.constBits(), 4 * img.width(), false);
    obs_leave_graphics();
}

void OBSQuickview::renderFrame(gs_effect_t *effect)
{
    if( !texture ) return;

    obs_enter_graphics();
    gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture);
    gs_draw_sprite(texture, 0, m_canvas.width(), m_canvas.height());
    obs_leave_graphics();
}
/*
void OBSQuickview::qmlStatus(QQuickWidget::Status status)
{
    QStringList out;
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
            out << "**ERRORS FOLLOW**";
            foreach( QQmlError err, m_quick->errors() )
            {
                qDebug() << " * " << err.toString();
                out << err.toString();
            }
            qmlWarnings(out);

            break;
        default:
            qDebug() << "obs-quickview: UNKNOWN";
            break;
    }
}

void OBSQuickview::qmlWarning(const QList<QQmlError> &warnings)
{
    QStringList out;
    foreach( QQmlError w, warnings )
    {
        qWarning() << w.toString();
        out << w.toString();
    }
    qmlWarnings(out);
}
*/
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

static void quickview_source_tick(void *data, float seconds)
{
    if(!data) return;
    OBSQuickview *s = (OBSQuickview *)data;
    Q_UNUSED(seconds);

    if( obs_source_active(s->source) )
        s->obsdraw();
}

static obs_properties_t *quickview_source_properties(void *data)
{
    Q_UNUSED(data);

    obs_properties_t *props = obs_properties_create();
    obs_properties_add_text(props, "file", obs_module_text("URL (eg: file:///C:/scenes/main.qml)"), OBS_TEXT_DEFAULT);
    obs_properties_add_bool(props, "unload", obs_module_text("Reload when made visible"));
    obs_properties_add_int(props, "width", obs_module_text("Width"), 1280, 4096, 1);
    obs_properties_add_int(props, "height", obs_module_text("Height"), 720, 4096, 1);

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
