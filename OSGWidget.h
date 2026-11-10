// ============================================================
// OSGWidget —— Qt 与 OSG 的"桥"
// 职责：提供一个能渲染 OSG 场景的 Qt 窗口部件，并把 Qt 的鼠标键盘
//       事件翻译给 OSG。这是【通用组件】，四个过程都不需要再改它。
// ============================================================
#ifndef OSGWIDGET_H
#define OSGWIDGET_H

#include <QOpenGLWidget>
#include <QTimer>
#include <osg/ref_ptr>
#include <osgViewer/Viewer>
#include <osgViewer/GraphicsWindow>
#include <osgGA/TrackballManipulator>
#include <osgGA/GUIEventAdapter>

class OSGWidget : public QOpenGLWidget
{
public:
    explicit OSGWidget(QWidget* parent = nullptr,
                       Qt::WindowFlags f = Qt::WindowFlags());
    ~OSGWidget() override;

    // 装入场景，并按场景包围球自动摆一个"斜上方俯视"的初始相机位
    void setScene(osg::Node* node);

    // 调整远/近裁剪面（场景达到公里级时必须放大 far，否则远处被裁掉）
    void setClip(double nearZ, double farZ);

    // 设置背景色
    void setBgColor(float r, float g, float b, float a = 1.f);

    // 切成正交俯视视角（PPI 显示器用），本过程不调用
    void setTopDownOrtho(double halfSize);

protected:
    // Qt 渲染回调：每帧调 viewer.frame() 驱动 OSG 完整渲染一遍
    void paintGL() override;
    void resizeGL(int w, int h) override;

    // —— 事件转发：Qt 事件 → OSG 事件队列 ——
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    static bool qtKeyToOsg(QKeyEvent* e, osgGA::GUIEventAdapter::KeySymbol& out);
    void applyProjection(int w, int h);

    osgViewer::Viewer m_viewer;                          // OSG 渲染器（无窗口版）
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> m_gwin; // "嵌入窗口"：借用 Qt 的 GL 上下文
    QTimer* m_timer = nullptr;                           // 60Hz 触发重绘
    qreal  m_scaleX = 1.0, m_scaleY = 1.0;               // 高分屏缩放（DPI）
    bool   m_ortho  = false;                             // 是否正交投影（过程5 用）
    double m_half   = 1000.0;                            // 正交视野半宽
    double m_nearZ  = 1.0, m_farZ = 100000.0;            // 裁剪面
};
#endif
