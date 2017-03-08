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
    : m_qmlComponent(NULL),
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

    // Now hook up the signals. For simplicy we don't differentiate between
    // renderRequested (only render is needed, no sync) and sceneChanged (polish and sync
    // is needed too).
    connect(m_quickWindow, &QQuickWindow::sceneGraphInitialized, this, &WindowSingleThreaded::createFbo);
    connect(m_quickWindow, &QQuickWindow::sceneGraphInvalidated, this, &WindowSingleThreaded::destroyFbo);
    connect(m_renderControl, &QQuickRenderControl::renderRequested, this, &WindowSingleThreaded::requestUpdate);
    connect(m_renderControl, &QQuickRenderControl::sceneChanged, this, &WindowSingleThreaded::requestUpdate);

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
        return;

    if( !m_frameReady )
        return;

    // Okay, redrawing, so... show it:
    if (!m_context->makeCurrent(m_offscreenSurface))
        return;

    // Polish, synchronize and render the next frame (into our fbo).  In this example
    // everything happens on the same thread and therefore all three steps are performed
    // in succession from here. In a threaded setup the render() call would happen on a
    // separate thread.
    m_renderControl->polishItems();
    m_renderControl->sync();
    m_renderControl->render();

    m_quickWindow->resetOpenGLState();
    QOpenGLFramebufferObject::bindDefault();

    m_context->functions()->glFlush();

    m_quickReady = true;

    // Get something onto the screen.
    if( m_fbo->bind() )
    {
/* TODO: Get this ... GL texture thing working, it's faster than copying fuckin' pixels:
        GLuint texid = m_fbo->texture();
        m_fbo->release();
        m_frameReady = false;
        emit updated(texid);
*/
        m_mutex.lock();
        m_image = m_fbo->toImage(true);
        m_mutex.unlock();
        m_fbo->release();
        m_frameReady = false;
        emit updated(0);
    }
}

static int counter{0};
void WindowSingleThreaded::requestUpdate()
{
    // Okay, want a redraw. Hide for now...
    //qDebug() << "requestUpdate..." << (counter++);
    m_frameReady = true;
}

QImage WindowSingleThreaded::getImage()
{
    return m_image;
}

#include <QDateTime>
qreal WindowSingleThreaded::calculateDelta(quint64 duration, qreal min, qreal max)
{
        quint64 now = QDateTime::currentMSecsSinceEpoch();
        qreal diff = (now % duration);
        qreal result = (max - min) * (diff / duration);
        return result + min;
}

void WindowSingleThreaded::resize(QSize newSize)
{
    //qDebug() << "Resizing: " << newSize;
    QWindow::resize(newSize);

    // If this is a resize after the scene is up and running, recreate the fbo and the
    // Quick item and scene.
    if (m_fbo && m_fbo->size() != size() * devicePixelRatio())
        resizeFbo();

    emit resized();
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
