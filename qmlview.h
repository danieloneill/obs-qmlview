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

class OBSQuickview : public QObject
{
Q_OBJECT
    QQuickWidget      *m_quick;

    QMutex  m_mutex;
    QImage  m_canvas;
    bool    m_enabled;
    bool    m_updated;
    bool    m_ready;

    quint8  *m_bits;

private:
    QGraphicsView           *m_view;
    QGraphicsScene          *m_scene;

    QSharedPointer<QQuickItemGrabResult>    m_snapper;

public:
    OBSQuickview(QObject *parent=NULL);
    ~OBSQuickview();

    obs_source_t *source;
    gs_texture  *texture;
    bool    m_persistent;

    void obsshow() { m_enabled = true; m_view->setUpdatesEnabled(true); }
    void obshide() { m_enabled = false; m_view->setUpdatesEnabled(false); }
    void obsdraw();
    void renderFrame(gs_effect_t *effect);

    void makeTexture();
    void loadUrl(QUrl url);
    void resize( quint32 w, quint32 h );
    void snap();

    quint32 width() { return m_quick->width(); }
    quint32 height() { return m_quick->height(); }

signals:
    void wantLoad(QUrl url);
    void wantResize(quint32 w, quint32 h);
    void wantSnap();

private slots:
    void doSnap();
    void doLoad(QUrl url);
    void doResize(quint32 w, quint32 h);

    void qmlStatus(QQuickWidget::Status status);
    void qmlFrame();
};
