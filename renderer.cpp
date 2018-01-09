/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "renderer.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOffscreenSurface>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickRenderControl>
#include <QCoreApplication>

#include <QDir>
#include <QUrlQuery>

class RenderControl : public QQuickRenderControl
{
public:
    RenderControl(QWindow *w) : m_window(w) { }
    QWindow *renderWindow(QPoint *offset) Q_DECL_OVERRIDE;

private:
    QWindow *m_window;
};

QWindow *RenderControl::renderWindow(QPoint *offset)
{
    if (offset)
        *offset = QPoint(0, 0);
    return m_window;
}

WindowSingleThreaded::WindowSingleThreaded()
    : m_frameChanged(false),
      m_qmlEngine(NULL),
      m_qmlComponent(NULL),
      m_rootItem(0),
      m_fbo(0),
      m_quickInitialized(false),
      m_quickReady(false),
      m_dpr(0)
{
    setSurfaceType(QSurface::OpenGLSurface);
    QWindow::resize(640, 480);

    QSurfaceFormat format;
    // Qt Quick may need a depth and stencil buffer. Always make sure these are available.
    format.setDepthBufferSize(16);
    format.setStencilBufferSize(8);
    setFormat(format);

    m_context = new QOpenGLContext;
    m_context->setFormat(format);
    m_context->create();

    m_offscreenSurface = new QOffscreenSurface;
    // Pass m_context->format(), not format. Format does not specify and color buffer
    // sizes, while the context, that has just been created, reports a format that has
    // these values filled in. Pass this to the offscreen surface to make sure it will be
    // compatible with the context's configuration.
    m_offscreenSurface->setFormat(m_context->format());
    m_offscreenSurface->create();

    m_renderControl = new RenderControl(this);

    // Create a QQuickWindow that is associated with out render control. Note that this
    // window never gets created or shown, meaning that it will never get an underlying
    // native (platform) window.
    m_quickWindow = new QQuickWindow(m_renderControl);
    m_quickWindow->setDefaultAlphaBuffer(true);
    m_quickWindow->setFormat(format);
    m_quickWindow->setColor(Qt::transparent);
    connect( m_quickWindow, SIGNAL(focusObjectChanged(QObject*)), this, SLOT(handleFocusChanged(QObject*)) );

    // Now hook up the signals. For simplicy we don't differentiate between
    // renderRequested (only render is needed, no sync) and sceneChanged (polish and sync
    // is needed too).
    connect(m_quickWindow, &QQuickWindow::sceneGraphInitialized, this, &WindowSingleThreaded::createFbo);
    connect(m_quickWindow, &QQuickWindow::sceneGraphInvalidated, this, &WindowSingleThreaded::destroyFbo);

    m_snapper = new Snapper(this);
    m_snapper->start();

    connect(this, &WindowSingleThreaded::snapWanted, m_snapper, &Snapper::snapRequested, Qt::QueuedConnection);
    connect(m_snapper, SIGNAL(resultReady()), this, SIGNAL(capped()), Qt::QueuedConnection );

    connect(m_renderControl, &QQuickRenderControl::renderRequested, this, &WindowSingleThreaded::triggerSnap);
    connect(m_renderControl, &QQuickRenderControl::sceneChanged, this, &WindowSingleThreaded::triggerSnap);

    // Just recreating the FBO on resize is not sufficient, when moving between screens
    // with different devicePixelRatio the QWindow size may remain the same but the FBO
    // dimension is to change regardless.
    connect(this, &QWindow::screenChanged, this, &WindowSingleThreaded::handleScreenChange);

    m_context->makeCurrent(m_offscreenSurface);
    m_renderControl->initialize(m_context);
}

WindowSingleThreaded::~WindowSingleThreaded()
{
    // Make sure the context is current while doing cleanup. Note that we use the
    // offscreen surface here because passing 'this' at this point is not safe: the
    // underlying platform window may already be destroyed. To avoid all the trouble, use
    // another surface that is valid for sure.
    m_context->makeCurrent(m_offscreenSurface);

    // Delete the render control first since it will free the scenegraph resources.
    // Destroy the QQuickWindow only afterwards.
    delete m_renderControl;

    if( m_qmlComponent )
        delete m_qmlComponent;
    if( m_rootItem )
        delete m_rootItem;
    delete m_quickWindow;
    if( m_qmlEngine )
        delete m_qmlEngine;
    delete m_fbo;

    m_context->doneCurrent();

    delete m_offscreenSurface;
    delete m_context;
}

void WindowSingleThreaded::createFbo()
{
    // The scene graph has been initialized. It is now time to create an FBO and associate
    // it with the QQuickWindow.
    m_dpr = devicePixelRatio();
    m_fbo = new QOpenGLFramebufferObject(size() * m_dpr, QOpenGLFramebufferObject::CombinedDepthStencil, GL_TEXTURE_2D, GL_RGBA8);
    m_quickWindow->setRenderTarget(m_fbo);
}

void WindowSingleThreaded::destroyFbo()
{
    delete m_fbo;
    m_fbo = 0;
}

void WindowSingleThreaded::render()
{
    if( !m_renderControl || !m_quickWindow )
    {
        if( !m_renderControl )
            qDebug() << "!m_renderControl";

        if( !m_quickWindow )
            qDebug() << "!m_quickWindow";


        return;
    }

#ifdef GLCOPY
    if( m_fboLocked )
        return;
#endif
    // Okay, redrawing, so... show it:
    if (!m_context->makeCurrent(m_offscreenSurface))
    {
        qDebug() << "Failed to make frame current!";
        return;
    }

    //qDebug() << "RENDER.";

    // Polish, synchronize and render the next frame (into our fbo).  In this example
    // everything happens on the same thread and therefore all three steps are performed
    // in succession from here. In a threaded setup the render() call would happen on a
    // separate thread.
    m_renderControl->polishItems();
    if( m_renderControl->sync() )
        m_renderControl->render();

    m_quickWindow->resetOpenGLState();
    QOpenGLFramebufferObject::bindDefault();

    m_context->functions()->glFlush();

    m_quickReady = true;

    // Get something onto the screen.
    if( m_fbo->bind() )
    {
#ifndef GLCOPY
        m_mutex.lock();
        //m_image = m_renderControl->grab().convertToFormat(QImage::Format_RGBA8888);
        m_image = m_fbo->toImage(true).convertToFormat(QImage::Format_RGBA8888);
        m_mutex.unlock();
        m_fbo->release();
        //qDebug() << "Image updated, m_frameReady now TRUE";
        emit capped();
#else
        // TODO: Get this ... GL texture thing working, it's faster than copying fuckin' pixels:
        GLuint texid = m_fbo->texture();
        m_fbo->release();
        m_frameReady = true;
        emit updated(texid);
#endif
    }
    else
        qDebug() << "Failed to bind FBO!";
}

#ifdef GLCOPY
GLuint WindowSingleThreaded::lockFbo()
{
    return m_fbo->takeTexture();

    if( m_fboLocked )
        return 0;

    GLuint texid = 0;
    if( m_fbo->bind() )
    {
        texid = m_fbo->texture();
        m_fboLocked = true;
    }
    return texid;
}

void WindowSingleThreaded::unlockFbo()
{
    if( m_fboLocked )
        m_fbo->release();
    m_fboLocked = false;
}
#endif

void WindowSingleThreaded::lock() { m_mutex.lock(); }
void WindowSingleThreaded::unlock() { m_mutex.unlock(); }

QImage *WindowSingleThreaded::getImage()
{
#ifdef MUTEXLOCKS
    QImage copy;
    m_mutex.lock();
    copy = m_image.copy();
    m_mutex.unlock();
    return copy;
#else
    return &m_image;
#endif
}
/*
#include <QDateTime>
qreal WindowSingleThreaded::calculateDelta(quint64 duration, qreal min, qreal max)
{
        quint64 now = QDateTime::currentMSecsSinceEpoch();
        qreal diff = (now % duration);
        qreal result = (max - min) * (diff / duration);
        return result + min;
}
*/
QVariant WindowSingleThreaded::getQuery()
{
    QVariantMap result;
    if( !m_url.hasQuery() )
        return false;

    QUrlQuery q(m_url);
    QPair<QString, QString> pair;
    foreach( pair, q.queryItems() )
    {
        result[ pair.first ] = pair.second;
    }
    return result;
}

void WindowSingleThreaded::resize(QSize newSize)
{
    qDebug() << "Resizing: " << newSize;
    QWindow::resize(newSize);

    // If this is a resize after the scene is up and running, recreate the fbo and the
    // Quick item and scene.
    if (m_fbo && m_fbo->size() != size() * devicePixelRatio())
        resizeFbo();

    emit resized();
}

void WindowSingleThreaded::manualUpdated(bool onoff)
{
    // TODO: For now, always leave the signals connected.
    m_forceRender = onoff;
    return;
/*
    if( onoff )
    {
        disconnect(m_renderControl, &QQuickRenderControl::renderRequested, this, &WindowSingleThreaded::triggerSnap);
        disconnect(m_renderControl, &QQuickRenderControl::sceneChanged, this, &WindowSingleThreaded::triggerSnap);
        qDebug() << "Disconnecting update signals.";
    }
    else
    {
        connect(m_renderControl, &QQuickRenderControl::renderRequested, this, &WindowSingleThreaded::triggerSnap, Qt::QueuedConnection);
        connect(m_renderControl, &QQuickRenderControl::sceneChanged, this, &WindowSingleThreaded::triggerSnap, Qt::QueuedConnection);
        qDebug() << "Connecting update signals.";
    }
*/
}

void WindowSingleThreaded::run()
{
    disconnect(m_qmlComponent, &QQmlComponent::statusChanged, this, &WindowSingleThreaded::run);

    if (m_qmlComponent->isError()) {
        QStringList msgs;
        QList<QQmlError> errorList = m_qmlComponent->errors();
        foreach (const QQmlError &error, errorList)
        {
            qWarning() << error.url() << error.line() << error;
            msgs << error.toString();
        }
        emit messages(msgs);
        return;
    }

    QObject *rootObject = m_qmlComponent->create();
    if (m_qmlComponent->isError()) {
        QList<QQmlError> errorList = m_qmlComponent->errors();
        foreach (const QQmlError &error, errorList)
            qWarning() << error.url() << error.line() << error;
        return;
    }

    m_rootItem = qobject_cast<QQuickItem *>(rootObject);
    if (!m_rootItem) {
        qWarning("run: Not a QQuickItem");
        delete rootObject;
        return;
    }

    // The root item is ready. Associate it with the window.
    m_rootItem->setParentItem(m_quickWindow->contentItem());

    // Update item and rendering related geometries.
    //updateSizes();

    // Initialize the render control and our OpenGL resources.
    //m_context->makeCurrent(m_offscreenSurface);
    //m_renderControl->initialize(m_context);
    m_quickInitialized = true;

    resizeFbo();
}

void WindowSingleThreaded::updateSizes()
{
    // Behave like SizeRootObjectToView.
    m_rootItem->setWidth(width());
    m_rootItem->setHeight(height());

    m_quickWindow->setGeometry(0, 0, width(), height());
}

void WindowSingleThreaded::addQmlPath()
{
    // FIXME: ... so kludge. wow.
    QStringList msgs;
    QDir d = QDir(QCoreApplication::applicationDirPath());
    QString bitpart = d.dirName();

    QString newPath = QDir::currentPath() + "/../../obs-plugins/" + bitpart + "/obs-qmlview/qml";
    msgs << "Adding to imports path: " << newPath;
    m_qmlEngine->addImportPath(newPath);
    foreach( QString path, m_qmlEngine->importPathList() )
        msgs << " >>  Import: " << path;

    newPath = QDir::currentPath() + "/../../obs-plugins/" + bitpart + "/obs-qmlview/plugins";
    msgs << "Adding to plugins path: " << newPath;
    m_qmlEngine->addPluginPath(newPath);
    foreach( QString path, m_qmlEngine->pluginPathList() )
        msgs << " >>  Plugin: " << path;

    m_loadMessages << msgs;
    //emit messages(msgs);
}

void WindowSingleThreaded::unload()
{
    m_quickInitialized = false;
    if( m_qmlComponent )
    {
      m_qmlComponent->deleteLater();
      m_rootItem->deleteLater();
      m_qmlEngine->deleteLater();
      m_qmlComponent = NULL;
      m_rootItem = NULL;
      m_qmlEngine = NULL;
    }
}

void WindowSingleThreaded::startQuick(const QUrl &url)
{
    m_quickInitialized = false;
    m_url = url;
    if( m_qmlComponent )
        unload();

    // Create a QML engine.
    m_qmlEngine = new QQmlEngine;
    if (!m_qmlEngine->incubationController())
        m_qmlEngine->setIncubationController(m_quickWindow->incubationController());

    m_qmlEngine->rootContext()->setContextProperty("engine", this);
    connect( m_qmlEngine, &QQmlEngine::warnings, this, &WindowSingleThreaded::handleWarnings );
    m_qmlEngine->setBaseUrl(url);
    addQmlPath();

    m_qmlComponent = new QQmlComponent(m_qmlEngine, url);
    if (m_qmlComponent->isLoading())
        connect(m_qmlComponent, &QQmlComponent::statusChanged, this, &WindowSingleThreaded::run);
    else
        run();
}

void WindowSingleThreaded::resizeFbo()
{
    if (m_rootItem && m_context->makeCurrent(m_offscreenSurface)) {
        destroyFbo();
        createFbo();
        m_context->doneCurrent();
        updateSizes();
        render();
    }
}

void WindowSingleThreaded::handleScreenChange()
{
    if (m_dpr != devicePixelRatio())
        resizeFbo();
}

void WindowSingleThreaded::handleWarnings(const QList<QQmlError> &warnings)
{
    QStringList msgs;
    foreach( QQmlError warn, warnings )
        msgs << warn.toString();
    emit messages(msgs);
}

void WindowSingleThreaded::handleFocusChanged(QObject *obj)
{
    qDebug() << "Focus changed!!! --------------- ";
    m_currentFocus = obj;
}

#include <QMouseEvent>
#include <QWheelEvent>
void WindowSingleThreaded::sendMouseClick(quint32 x, quint32 y, qint32 type, bool onoff, quint8 click_count)
{
    Qt::MouseButton btn = Qt::NoButton;
    if( type == 0 )
        btn = Qt::LeftButton;
    if( type == 1 )
        btn = Qt::MiddleButton;
    if( type == 2 )
        btn = Qt::RightButton;

    QMouseEvent *event = new QMouseEvent( (onoff) ? QEvent::MouseButtonRelease : QEvent::MouseButtonPress, QPointF(x, y), btn, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::postEvent (m_quickWindow, event);
}

void WindowSingleThreaded::sendMouseMove(quint32 x, quint32 y, bool leaving)
{
    QMouseEvent *event = new QMouseEvent( QEvent::MouseMove, QPointF(x, y), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::postEvent (m_quickWindow, event);
}

void WindowSingleThreaded::sendMouseWheel(qint32 xdelta, qint32 ydelta)
{
    QWheelEvent *event = new QWheelEvent( QPointF(), QPointF(), QPoint(), QPoint(xdelta, ydelta), 0, xdelta != 0 ? Qt::Vertical : Qt::Horizontal, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase);
    QCoreApplication::postEvent (m_quickWindow, event);
}

void WindowSingleThreaded::sendKey(quint32 keycode, quint32 vkey, quint32 modifiers, const char *text, bool updown)
{
    int newCode = keycode;
    if( keycode == 36 )
        newCode = Qt::Key_Enter;
    else if( keycode == 111 )
        newCode = Qt::Key_Up;
    else if( keycode == 116 )
        newCode = Qt::Key_Down;
    else if( keycode == 113 )
        newCode = Qt::Key_Left;
    else if( keycode == 114 )
        newCode = Qt::Key_Right;
    else if( keycode == 110 )
        newCode = Qt::Key_Home;
    else if( keycode == 112 )
        newCode = Qt::Key_PageUp;
    else if( keycode == 117 )
        newCode = Qt::Key_PageDown;
    else if( keycode == 115 )
        newCode = Qt::Key_End;
    else if( text[0] >= ' ' && text[0] <= '~' )
    {
        qDebug() << "Key " << ( updown ? QString("Up") : QString("Down") ) << ": " << QString::fromLocal8Bit(text);
        QKeySequence seq( text );
        newCode = seq[0];
    }
    else
    {
        qDebug() << "Got keycode: " << keycode << " / Mapped is " << QChar(keycode) << " (Down:" << (!updown) << ")"; //.unicode();
    }

    //QKeyEvent *event = new QKeyEvent( (!updown) ? QEvent::KeyPress : QEvent::KeyRelease, keycode, Qt::NoModifier, QString::fromLocal8Bit(text) );
    QKeyEvent *event = new QKeyEvent( (!updown) ? QEvent::KeyPress : QEvent::KeyRelease, newCode, Qt::NoModifier, keycode, vkey, modifiers, QString::fromLocal8Bit(text) );
    QCoreApplication::postEvent (m_rootItem, event);
}

void WindowSingleThreaded::sendFocus(bool onoff)
{
    qDebug() << "Focus: " << onoff;
    QFocusEvent *event = new QFocusEvent( onoff ? QFocusEvent::FocusIn : QFocusEvent::FocusOut );
    QCoreApplication::postEvent (m_quickWindow, event);
}

// Triggered by the render control (QML engine wants a redraw):
void WindowSingleThreaded::triggerSnap()
{
    //qDebug() << " ++ Frame changed!";
    m_frameChanged = true;
    /*
    if( m_wantFrame )
    {
        m_wantFrame = false;
        emit snapWanted();
    }
    */
}

void WindowSingleThreaded::wantFrame()
{
    if( !m_frameChanged )
        return;
    m_frameChanged = false;
    emit snapWanted();
}

Snapper::Snapper(WindowSingleThreaded *parent)
{
    m_parent = parent;
    m_snapping = false;
}

Snapper::~Snapper()
{

}

void Snapper::snapRequested()
{
    if( m_snapping )
    {
        qDebug() << "m_snapping";
        return;
    }

    m_snapping = true;
    m_parent->render();
    emit resultReady();
    m_snapping = false;
}

