#include "swipecanvas.h"
#include "SwipeModel.h"
#include "control_socket.h"
#include <QPainter>
#include <QPen>
#include <QColor>
#include <QDebug>
#include <QMouseEvent>
#include <algorithm>
#include <QMatrix4x4>
#include <QOpenGLTexture>
#include <QOpenGLShaderProgram>
#include <QMutexLocker>
#include <QOpenGLFunctions>

static const char* VERTEX_SHADER_CODE = R"(
    #version 330 core
    layout(location = 0) in vec2 position;
    layout(location = 1) in vec2 texcoord;
    uniform mat4 u_ProjectionMatrix;
    out vec2 vTexCoord;
    void main() {
        vTexCoord = texcoord;
        gl_Position = u_ProjectionMatrix * vec4(position, 0.0, 1.0);
    }
)";

static const char* FRAGMENT_SHADER_CODE = R"(
    #version 330 core
    in vec2 vTexCoord;
    out vec4 fragColor;
    uniform sampler2D textureY;
    uniform sampler2D textureU;
    uniform sampler2D textureV;
    void main() {
        float y = texture(textureY, vTexCoord).r;
        float u = texture(textureU, vTexCoord).r - 0.5;
        float v = texture(textureV, vTexCoord).r - 0.5;
        // Konwersja BT.601 (standard dla Android ScreenRecord)
        float r = y + 1.402 * v;
        float g = y - 0.34414 * u - 0.71414 * v;
        float b = y + 1.772 * u;
        fragColor = vec4(r, g, b, 1.0);
    }
)";

SwipeCanvas::SwipeCanvas(SwipeModel *model, ControlSocket *controlSocket, QWidget *parent)
    : QOpenGLWidget(parent),
      m_model(model),
      m_controlSocket(controlSocket),
      m_program(nullptr),
      m_texY(nullptr), m_texU(nullptr), m_texV(nullptr),
      m_textureInited(false),
      m_videoW(0), m_videoH(0),
      m_deviceWidth(720), m_deviceHeight(1280),
      m_scaleFactor(1.0), m_offsetX(0), m_offsetY(0),
      m_dragging(false)
{
    setMouseTracking(true);
    m_remoteFinishTimer.setSingleShot(true);
    m_remoteFinishTimer.setInterval(160);
    connect(&m_remoteFinishTimer, &QTimer::timeout, this, &SwipeCanvas::onRemoteTouchFinished);
}

SwipeCanvas::~SwipeCanvas() {
    makeCurrent();
    delete m_texY;
    delete m_texU;
    delete m_texV;
    delete m_program;
    m_vbo.destroy();
    m_vao.destroy();
    doneCurrent();
}

void SwipeCanvas::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, VERTEX_SHADER_CODE);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, FRAGMENT_SHADER_CODE);
    m_program->link();
    float vertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };
    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices, sizeof(vertices));
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    m_vao.release();
}

void SwipeCanvas::initTextures(int width, int height) {
	if (!context()) return;
    makeCurrent();
    if (m_texY) { m_texY->destroy(); delete m_texY; m_texY = nullptr; }
    if (m_texU) { m_texU->destroy(); delete m_texU; m_texU = nullptr; }
    if (m_texV) { m_texV->destroy(); delete m_texV; m_texV = nullptr; }
    auto createTex = [&](int w, int h) {
        QOpenGLTexture* tex = new QOpenGLTexture(QOpenGLTexture::Target2D);
        tex->setSize(w, h);
        tex->setFormat(QOpenGLTexture::R8_UNorm);
        tex->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
        tex->setMinificationFilter(QOpenGLTexture::Linear);
        tex->setMagnificationFilter(QOpenGLTexture::Linear);
        tex->setWrapMode(QOpenGLTexture::ClampToEdge);
        return tex;
    };
    m_texY = createTex(width, height);
    m_texU = createTex(width / 2, height / 2);
    m_texV = createTex(width / 2, height / 2);
    m_videoW = width;
    m_videoH = height;
    m_textureInited = true;
}

void SwipeCanvas::onFrameReady(AVFramePtr frame) {
    QMutexLocker locker(&m_frameMutex);
    m_currentFrame = frame;
    update();
}

void SwipeCanvas::paintGL() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    AVFramePtr frame;
    {
        QMutexLocker locker(&m_frameMutex);
        if (!m_currentFrame) return;
        frame = m_currentFrame;
    }
    if (m_videoW != frame->width || m_videoH != frame->height) {
        m_videoW = frame->width;
		m_videoH = frame->height;
		m_deviceWidth = 720;
		m_deviceHeight = 1280;

/*
		int baseMin = std::min(m_deviceWidth, m_deviceHeight);
		int baseMax = std::max(m_deviceWidth, m_deviceHeight);
		if (m_videoW > m_videoH) {
			m_deviceWidth = baseMax;
			m_deviceHeight = baseMin;
		} else {
			m_deviceWidth = baseMin;
			m_deviceHeight = baseMax;
		}
*/
		
        initTextures(m_videoW, m_videoH);
        calculateScale();
        qDebug() << "PAINT_GL: Zmiana rozdzielczości na" << m_videoW << "x" << m_videoH;
        update(); 
        return; 
	}
	if (!m_textureInited || !m_texY || !m_texY->isCreated()) return;
    try {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (frame->linesize[0] < frame->width) return;
        m_texY->bind(0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[0]);
        m_texY->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, frame->data[0]);
        m_texU->bind(1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[1]);
        m_texU->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, frame->data[1]);
        m_texV->bind(2);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, frame->linesize[2]);
        m_texV->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, frame->data[2]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        m_program->bind();
        m_vao.bind();
        QMatrix4x4 projection;
        projection.ortho(0, width(), height(), 0, -1.0, 1.0);
        projection.translate(m_offsetX, m_offsetY, 0.0);
        projection.scale(m_videoW * m_scaleFactor, m_videoH * m_scaleFactor, 1.0);
        projection.translate(0.5f, 0.5f, 0.0f);
        projection.scale(0.5f, 0.5f, 1.0f);
        m_program->setUniformValue("u_ProjectionMatrix", projection);
        m_program->setUniformValue("textureY", 0);
        m_program->setUniformValue("textureU", 1);
        m_program->setUniformValue("textureV", 2);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_vao.release();
        m_program->release();
    } catch (...) {
        qCritical() << "Krytyczny błąd renderowania w paintGL";
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_dragging) {
        p.setPen(QPen(Qt::yellow, 2, Qt::SolidLine));
        p.drawLine(m_start, m_end);
        p.setBrush(Qt::green);
        p.drawEllipse(m_start, 4, 4);
    }
    if (!m_remotePath.isEmpty()) {
        p.setPen(QPen(Qt::red, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (m_remotePath.size() > 1) {
            for (int i = 0; i < m_remotePath.size() - 1; ++i) {
                p.drawLine(m_remotePath[i], m_remotePath[i+1]);
            }
        }
        p.setBrush(Qt::red);
        p.drawEllipse(m_remotePath.last(), 5, 5);
    }
}

void SwipeCanvas::resizeGL(int w, int h) {calculateScale();}

void SwipeCanvas::calculateScale() {
    if (m_videoW <= 0 || m_videoH <= 0 || width() <= 0 || height() <= 0) return;
    float widgetW = (float)width();
    float widgetH = (float)height();
    float videoW  = (float)m_videoW;
    float videoH  = (float)m_videoH;
    float scaleX = widgetW / videoW;
    float scaleY = widgetH / videoH;
    m_scaleFactor = std::min(scaleX, scaleY);
    m_offsetX = (widgetW - (videoW * m_scaleFactor)) / 2.0f;
    m_offsetY = (widgetH - (videoH * m_scaleFactor)) / 2.0f;
}

/*
QPoint SwipeCanvas::mapToDevice(QPoint p) {
    float localX = (float)p.x() - m_offsetX;
    float localY = (float)p.y() - m_offsetY;
    if (m_scaleFactor <= 0) return QPoint(0,0);
    float videoX = localX / m_scaleFactor;
    float videoY = localY / m_scaleFactor;
    float normX = videoX / (float)m_videoW;
    float normY = videoY / (float)m_videoH;
    int finalX = (int)(normX * 4096.0f);
    int finalY = (int)(normY * 4096.0f);
    return QPoint(qBound(0, finalX, 4096), qBound(0, finalY, 4096));
}
*/


QPoint SwipeCanvas::mapToDevice(QPoint p) {
    float localX = (float)p.x() - m_offsetX;
    float localY = (float)p.y() - m_offsetY;
    if (m_scaleFactor <= 0) return QPoint(0,0);
    float videoX = localX / m_scaleFactor;
    float videoY = localY / m_scaleFactor;
    float normX = videoX / (float)m_videoW;
    float normY = videoY / (float)m_videoH;
    int finalX, finalY;
    if (m_videoW > m_videoH) { 
        finalX = (int)(normY * 720.0f);
        finalY = (int)((1.0f - normX) * 1280.0f);
    } else {
        finalX = (int)(normX * 720.0f);
        finalY = (int)(normY * 1280.0f);
    }
    return QPoint(qBound(0, finalX, 720), qBound(0, finalY, 1280));
}

void SwipeCanvas::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        m_start = e->pos();
        m_end = e->pos();
        m_dragging = true;
        QPoint devPos = mapToDevice(m_start);
        if (m_controlSocket) m_controlSocket->sendTouchDown(devPos.x(), devPos.y());
        update();}}

void SwipeCanvas::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragging) {
        m_end = e->pos();
        QPoint devPos = mapToDevice(m_end);
        if (m_controlSocket) m_controlSocket->sendTouchMove(devPos.x(), devPos.y());
        update();}}

void SwipeCanvas::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        QPoint devStart = mapToDevice(m_start);
        QPoint devEnd = mapToDevice(e->pos());
        if (m_controlSocket) m_controlSocket->sendTouchUp(devEnd.x(), devEnd.y());
        if ((m_start - e->pos()).manhattanLength() < 10) {
            m_model->addTap(devStart.x(), devStart.y());
        } else {
            m_model->addSwipe(devStart.x(), devStart.y(), devEnd.x(), devEnd.y(), 300);}
        update();}}

void SwipeCanvas::setDeviceResolution(int width, int height) {
    m_deviceWidth = width;
    m_deviceHeight = height;
    calculateScale();}

void SwipeCanvas::setStatus(const QString &msg, bool isError) {
    if (isError) {
        qCritical() << "Canvas Error:" << msg;
    } else {
        qInfo() << "Canvas Status:" << msg;
    }
    update();}

void SwipeCanvas::onRemoteTouchEvent(int axis, int value) {
    if (axis == 0x35) m_remoteX = value;
    else if (axis == 0x36) m_remoteY = value;
    if (m_remoteX >= 0 && m_remoteY >= 0) {
        float normX, normY;
        if (m_videoW > m_videoH) { 
            normY = (float)m_remoteX / 720.0f;
            normX = 1.0f - ((float)m_remoteY / 1280.0f);
        } else {
            normX = (float)m_remoteX / 720.0f;
            normY = (float)m_remoteY / 1280.0f;
        }
        int wx = (int)(m_offsetX + normX * (m_videoW * m_scaleFactor));
        int wy = (int)(m_offsetY + normY * (m_videoH * m_scaleFactor));
        QPoint widgetPoint(wx, wy);
        QPoint devicePoint(m_remoteX, m_remoteY);
        if (m_remotePath.isEmpty() || m_remotePath.last() != widgetPoint) {
            m_remotePath.append(widgetPoint);
        }
        if (m_remoteDevicePath.isEmpty() || m_remoteDevicePath.last() != devicePoint) {
            m_remoteDevicePath.append(devicePoint);
        }
    }
    m_remoteFinishTimer.start(); 
    update();
}


/*
void SwipeCanvas::onRemoteTouchEvent(int axis, int value) {
    qDebug() << "CANVAS ODEBRAŁ DOTYK: " << axis << value;
    if (axis == 0x35) m_remoteX = value;
    else if (axis == 0x36) m_remoteY = value;
    if (m_remoteX >= 0 && m_remoteY >= 0) {
        int devX = qBound(0, m_remoteX, m_deviceWidth - 1);
        int devY = qBound(0, m_remoteY, m_deviceHeight - 1);
        float normX = (m_deviceWidth > 0) ? (float)devX / m_deviceWidth : 0;
        float normY = (m_deviceHeight > 0) ? (float)devY / m_deviceHeight : 0;
        int wx = (int)(m_offsetX + normX * (m_videoW * m_scaleFactor));
        int wy = (int)(m_offsetY + normY * (m_videoH * m_scaleFactor));
        QPoint widgetPoint(wx, wy);
        QPoint devicePoint(devX, devY);
        if (m_remotePath.isEmpty() || m_remotePath.last() != widgetPoint) {
            m_remotePath.append(widgetPoint);}
        if (m_remoteDevicePath.isEmpty() || m_remoteDevicePath.last() != devicePoint) {
            m_remoteDevicePath.append(devicePoint);}}
    m_remoteFinishTimer.start(); 
    update();}
*/


void SwipeCanvas::onRemoteTouchFinished() {
    qDebug() << "Koniec dotyku fizycznego. Zebrano punktów:" << m_remoteDevicePath.size();
    if (!m_remoteDevicePath.isEmpty() && m_model) {
        QPoint devStart = m_remoteDevicePath.first();
        QPoint devEnd = m_remoteDevicePath.last();
        const int TAP_THRESHOLD_PIXELS = 20;
        int dist = qAbs(devStart.x() - devEnd.x()) + qAbs(devStart.y() - devEnd.y());

        qDebug() << "Dystans ruchu:" << dist << "Threshold:" << TAP_THRESHOLD_PIXELS;

        if (dist <= TAP_THRESHOLD_PIXELS) {
            qDebug() << "!!! REJESTRUJĘ TAP (FIZYCZNY) !!!";
            m_model->addTap(devStart.x(), devStart.y());
        } else {
            qDebug() << "!!! REJESTRUJĘ SWIPE (FIZYCZNY) !!!";
            m_model->addSwipe(devStart.x(), devStart.y(), devEnd.x(), devEnd.y(), 300);
        }
    }
    m_remoteDevicePath.clear();
    m_remotePath.clear();
    m_remoteX = -1;
    m_remoteY = -1;
    update();
}
