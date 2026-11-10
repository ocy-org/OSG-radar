// ============================================================
// 程序入口：只做三件事 —— 初始化 Qt、配置 OpenGL 表面、启动主窗口
// ============================================================
#include <QApplication>
#include <QSurfaceFormat>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    // 告诉 Qt 使用系统桌面 OpenGL（而非 ANGLE/DirectX 转译层）。
    // OSG 基于桌面版 OpenGL，这一行必须放在 QApplication 构造之前。
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);

    QApplication app(argc, argv);

    QSurfaceFormat fmt;
    fmt.setDepthBufferSize(24);//   深度 24 位 —— 3D 遮挡判断（飞机在网格前面还是后面）必需
    fmt.setStencilBufferSize(8);//   模板 8 位 —— 本项目暂用不到，但留着无害
    fmt.setSamples(4);//   4 倍多重采样 —— 抗锯齿，让网格线/航线平滑
    QSurfaceFormat::setDefaultFormat(fmt);// 全局默认，影响此后创建的所有表面

    // 命令行第 1 个参数 = 飞机模型路径（可选），例如：
    //   Process1.exe D:/OpenSceneGraph-Data/cessna.osg
    // 不传则先找 cessna.osg，再失败就用黄色占位盒兜底
    MainWindow w(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
    w.setWindowTitle(QStringLiteral("网格 + 飞机 + 五角星定向飞行"));
    w.resize(1280, 800);
    w.show();
    return app.exec();
}
