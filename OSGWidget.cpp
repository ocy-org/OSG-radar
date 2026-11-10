#include "OSGWidget.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

OSGWidget::OSGWidget(QWidget* parent, Qt::WindowFlags f)
    : QOpenGLWidget(parent, f)
{
    setMouseTracking(true);          // 不按键也接收 mouseMove（拖拽预览用）
    setFocusPolicy(Qt::StrongFocus); // 可接收键盘

    // 高分屏适配：Qt 的逻辑像素 ≠ 物理像素，OSG 需要物理像素
    m_scaleX = devicePixelRatioF();
    m_scaleY = devicePixelRatioF();

    // GraphicsWindowEmbedded：OSG 不自己创建窗口和 GL 上下文，
    // 而是借用在 paintGL 时刻 Qt 已绑定好的那个上下文 —— 这就是
    // "Qt 主导渲染循环、OSG 只负责画" 的集成方案核心。
    m_gwin = new osgViewer::GraphicsWindowEmbedded(x(), y(), width(), height());

    osg::Camera* cam = m_viewer.getCamera();
    cam->setGraphicsContext(m_gwin.get());
    cam->setClearColor(osg::Vec4(0.08f, 0.10f, 0.12f, 1.0f));

    // 单线程渲染：frame() 在 Qt 的 GUI 线程里同步执行，简单且安全。
    // （多线程渲染是性能优化课题，初学阶段单线程最稳）
    m_viewer.setThreadingModel(osgViewer::ViewerBase::SingleThreaded);
    m_viewer.setKeyEventSetsDone(0);  // 禁掉 OSG 默认的 Esc 退出
    m_viewer.realize();
    // 关闭"小特征剔除"：屏幕投影小于 2px 的节点默认会被整体跳过。
    m_viewer.getCamera()->setSmallFeatureCullingPixelSize(0.0f);

    // 60Hz 定时器触发 Qt 重绘 → paintGL → viewer.frame()
    // 渲染节奏归 Qt，业务数据节奏归 MainWindow 的另一个定时器，互不干扰
    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    m_timer->start();
}

OSGWidget::~OSGWidget() { m_timer->stop(); }

void OSGWidget::setScene(osg::Node* node)
{
    m_viewer.setSceneData(node);//设置场景

    // 按场景包围球自动算初始相机位置：
    // 眼睛放在"包围球半径 2.5 倍远、斜上方"，保证整个场景入画
    osg::Vec3d center(0, 0, 0);
    double radius = 10.0;
    if (node) {
        const osg::BoundingSphere bs = node->getBound();// 取场景的包围球，返回能包住整棵场景树的球体（中心 + 半径）
        if (bs.valid()) { center = osg::Vec3d(bs.center()); radius = bs.radius(); }
    }
    const double dist = radius * 2.5 + 1.0;
    const osg::Vec3d eye = center + osg::Vec3d(0.0, -dist, dist * 0.5);//斜上方俯视

    // 轨迹球操纵器：左键旋转 / 右键平移 / 滚轮缩放
    osg::ref_ptr<osgGA::TrackballManipulator> manip = new osgGA::TrackballManipulator;
    manip->setAllowThrow(false);   // 关掉松手后的惯性甩动
    manip->setHomePosition(eye, center, osg::Vec3d(0.0, 0.0, 1.0));//把这个机位登记为“Home 位”
    //TrackballManipulator 轨迹球模式
    //FlyManipulator 飞行模式
    //OrbitManipulator 环绕模式
    m_viewer.setCameraManipulator(manip.get());//装上漫游器,负责响应鼠标来旋转/缩放/平移视角的相机控制器
}

void OSGWidget::setClip(double nearZ, double farZ)
{ m_nearZ = nearZ; m_farZ = farZ; }

void OSGWidget::setBgColor(float r, float g, float b, float a)
{ m_viewer.getCamera()->setClearColor(osg::Vec4(r, g, b, a)); }

void OSGWidget::setTopDownOrtho(double halfSize)
{
    m_ortho = true;                          // 正交投影：无近大远小（PPI 需要正圆）
    m_half  = halfSize;
    m_viewer.setCameraManipulator(nullptr);  // 固定视角，不要轨迹球
    // 相机悬在正上方 20000m，观察原点，"上方向"指北（+Y）→ 屏幕上方 = 北
    m_viewer.getCamera()->setViewMatrixAsLookAt(
        osg::Vec3d(0, 0, 20000), osg::Vec3d(0, 0, 0), osg::Vec3d(0, 1, 0));
    applyProjection(width(), height());
}

// 根据窗口宽高比设置投影矩阵（每次 resize 都要重算，否则画面变形）
void OSGWidget::applyProjection(int w, int h)
{
    osg::Camera* cam = m_viewer.getCamera();
    const double aw = w * m_scaleX, ah = h * m_scaleY;
    const double aspect = (ah > 0) ? aw / ah : 1.0;

    if (m_ortho) {
        // 正交：短边贴合 m_half，长边按宽高比扩展，保证圆不压成椭圆
        if (aspect >= 1.0)
            cam->setProjectionMatrixAsOrtho(-m_half*aspect, m_half*aspect,
                                            -m_half, m_half, m_nearZ, m_farZ);
        else
            cam->setProjectionMatrixAsOrtho(-m_half, m_half,
                                            -m_half/aspect, m_half/aspect,
                                            m_nearZ, m_farZ);
    } else {
        // 透视：30° 垂直视场角（人眼自然视角附近）
        cam->setProjectionMatrixAsPerspective(30.0, aspect, m_nearZ, m_farZ);
    }
}

void OSGWidget::paintGL() { m_viewer.frame(); }

void OSGWidget::resizeGL(int w, int h)
{
    if (!m_gwin.valid()) return;
    m_scaleX = devicePixelRatioF();
    m_scaleY = devicePixelRatioF();
    // 通知 OSG 视口尺寸变了（注意坐标乘了 DPI 缩放）
    m_gwin->getEventQueue()->windowResize(0, 0, w*m_scaleX, h*m_scaleY);
    m_gwin->resized(0, 0, w*m_scaleX, h*m_scaleY);
    m_viewer.getCamera()->setViewport(0, 0, w*m_scaleX, h*m_scaleY);
    applyProjection(w, h);
}

// —— Qt 键码 → OSG 键码 ——（两套框架的键值枚举不同，需手动映射）
bool OSGWidget::qtKeyToOsg(QKeyEvent* e, osgGA::GUIEventAdapter::KeySymbol& out)
{
    switch (e->key()) {
    case Qt::Key_Left:     out = osgGA::GUIEventAdapter::KEY_Left;      return true;
    case Qt::Key_Right:    out = osgGA::GUIEventAdapter::KEY_Right;     return true;
    case Qt::Key_Up:       out = osgGA::GUIEventAdapter::KEY_Up;        return true;
    case Qt::Key_Down:     out = osgGA::GUIEventAdapter::KEY_Down;      return true;
    case Qt::Key_Space:    out = osgGA::GUIEventAdapter::KEY_Space;     return true;
    case Qt::Key_Home:     out = osgGA::GUIEventAdapter::KEY_Home;      return true;
    case Qt::Key_PageUp:   out = osgGA::GUIEventAdapter::KEY_Page_Up;   return true;
    case Qt::Key_PageDown: out = osgGA::GUIEventAdapter::KEY_Page_Down; return true;
    default: return false;
    }
}

void OSGWidget::keyPressEvent(QKeyEvent* e)
{
    osgGA::GUIEventAdapter::KeySymbol ks;
    if (qtKeyToOsg(e, ks)) m_gwin->getEventQueue()->keyPress(ks);
    else QOpenGLWidget::keyPressEvent(e);
}

void OSGWidget::mousePressEvent(QMouseEvent* e)
{
    // Qt 按钮 → OSG 按钮编号（1左 2中 3右）
    int button = 0;
    if (e->button() == Qt::LeftButton)   button = 1;
    if (e->button() == Qt::MiddleButton) button = 2;
    if (e->button() == Qt::RightButton)  button = 3;
    // ★ Y 轴翻转：Qt 原点在左上角，OpenGL/OSG 原点在左下角
    m_gwin->getEventQueue()->mouseButtonPress(
        e->x() * m_scaleX, (height() - e->y()) * m_scaleY, button);
}

void OSGWidget::mouseReleaseEvent(QMouseEvent* e)
{
    int button = 0;
    if (e->button() == Qt::LeftButton)   button = 1;
    if (e->button() == Qt::MiddleButton) button = 2;
    if (e->button() == Qt::RightButton)  button = 3;
    m_gwin->getEventQueue()->mouseButtonRelease(
        e->x() * m_scaleX, (height() - e->y()) * m_scaleY, button);
}

void OSGWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    m_gwin->getEventQueue()->mouseDoubleButtonPress(
        e->x() * m_scaleX, (height() - e->y()) * m_scaleY,
        e->button() == Qt::LeftButton ? 1 : 0);
}

void OSGWidget::mouseMoveEvent(QMouseEvent* e)
{
    m_gwin->getEventQueue()->mouseMotion(
        e->x() * m_scaleX, (height() - e->y()) * m_scaleY);
}

void OSGWidget::wheelEvent(QWheelEvent* e)
{
    m_gwin->getEventQueue()->mouseScroll(
        e->angleDelta().y() >= 0 ? osgGA::GUIEventAdapter::SCROLL_UP
                                 : osgGA::GUIEventAdapter::SCROLL_DOWN);
}
