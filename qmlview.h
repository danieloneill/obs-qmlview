#include <QtQuick>
#include <QtWidgets/QtWidgets>
#include <QtQuickWidgets/QQuickWidget>

#include <QMutex>
#include <QTimer>
#include <QPointer>

#include <obs-module.h>
#include <obs-source.h>
#include <util/dstr.h>
#include <graphics/graphics.h>

class WindowSingleThreaded;

class OBSQuickview : public QObject
{
Q_OBJECT
    WindowSingleThreaded    *m_quickView;

    QUrl    m_source;

    QSize   m_size;
    QMutex  m_mutex;
    QImage  m_canvas;
    bool    m_enabled;
    bool    m_updated;
    bool    m_ready;
    bool    m_sceneChanged;

    // Or... just... shared context stuff:
    GLuint  m_texid;
    quint8  *m_bits;

private:
    void addPluginsPath();
    void addQmlPath();

public:
    OBSQuickview(QObject *parent=NULL);
    ~OBSQuickview();

    obs_source_t *source;
    gs_texture  *texture;
    bool    m_persistent;

    void obsshow();
    void obshide();
    void obsdraw();
    void renderFrame(gs_effect_t *effect);

    void makeWidget();
    void makeTexture();
    void loadUrl(QUrl url);
    void resize( quint32 w, quint32 h );
    void snap();

public slots:
    quint32 width() { return m_canvas.width(); }
    quint32 height() { return m_canvas.height(); }

signals:
    void wantLoad();
    void wantUnload();
    void wantResize(quint32 w, quint32 h);
    void wantSnap();
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
    void qmlFrame(GLuint texid=0);
};
