#include <QtQuick>
#include <QtWidgets/QtWidgets>
#include <QtQuickWidgets/QQuickWidget>

#include <QMutex>
#include <QPointer>

#include <obs-module.h>
#include <obs-source.h>
#include <util/dstr.h>
#include <graphics/graphics.h>

class WindowSingleThreaded;
class FrameCounter;

class OBSQuickview : public QObject
{
Q_OBJECT
    QUrl    m_source;
    QStringList m_messages;

    QSize   m_size;
    QMutex  m_mutex;
    QImage  m_canvas;
    bool    m_enabled;
    bool    m_ready;
    bool    m_sceneRendered;

    // Or... just... shared context stuff:
    GLuint  m_texid;
    quint8  *m_bits;

private:
    void addPluginsPath();

public:
    OBSQuickview(QObject *parent=NULL);
    ~OBSQuickview();

    WindowSingleThreaded    *m_quickView;

    obs_source_t *source;
    gs_texture  *texture;
    bool    m_persistent;

    FrameCounter    *m_renderCounter, *m_drawCounter, *m_qmlFrameCounter;

    // Limit FPS to a specific rate:
    bool    m_frameLimited;
    double  m_nextFrame;
    quint32 m_fps;

    void obsshow();
    void obshide();
    bool obsdraw();
    void renderFrame(gs_effect_t *effect);
    void renderFrameCustom(gs_effect_t *effect);

    void makeWidget();
    void makeTexture();
    void loadUrl(QUrl url);
    void resize( quint32 w, quint32 h );
    void tick(quint64 seconds);

    bool frameDue(); // is it time for a new frame?
    void frameSynced(); // mark last frame time as now

    // Interaction:
    void sendMouseClick(quint32 x, quint32 y, qint32 type, bool onoff, quint8 click_count);
    void sendMouseMove(quint32 x, quint32 y, bool leaving);
    void sendMouseWheel(qint32 xdelta, qint32 ydelta);
    void sendKey(quint32 keycode, bool updown);

public slots:
    quint32 width() { return m_canvas.width(); }
    quint32 height() { return m_canvas.height(); }

signals:
    void wantLoad();
    void wantUnload();
    void wantResize(quint32 w, quint32 h);
    void frameRendered();
    void qmlWarnings(QStringList warnings);

private slots:
    void doSnap();
    void doLoad();
    void doUnload();
    void doResize(quint32 w, quint32 h);
/*
    void qmlStatus(QQuickWidget::Status status);
    void qmlWarning(const QList<QQmlError> &warnings);
*/
    void qmlFrame();
    void qmlCheckFrame();
    void qmlCopy();
};
