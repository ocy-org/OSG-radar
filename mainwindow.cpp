#include "mainwindow.h"
#include "situationscene.h"
#include "geojson.h"     // ★ 新增：GeoJSON 地图加载
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>
#include <QVector>
#include <QString>
#include <osg/Geode>
#include <osg/ShapeDrawable>
#include <osg/MatrixTransform>
#include <osg/Matrix>
#include <osg/Material>
#include <osgDB/ReadFile>

// 没有模型文件时的兜底"飞机"（黄色十字盒，保证任何机器都能跑通）
static osg::ref_ptr<osg::Node> makePlaceholderAircraft()
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    osg::ref_ptr<osg::ShapeDrawable> body = new osg::ShapeDrawable(
        new osg::Box(osg::Vec3(0, 0, 0), 60.f, 260.f, 30.f));  // 机身
    body->setColor(osg::Vec4(0.95f, 0.95f, 0.35f, 1.f));
    osg::ref_ptr<osg::ShapeDrawable> wing = new osg::ShapeDrawable(
        new osg::Box(osg::Vec3(0, 0, 45.f), 340.f, 55.f, 18.f)); // 机翼
    wing->setColor(osg::Vec4(0.9f, 0.5f, 0.1f, 1.f));
    geode->addDrawable(body.get());
    geode->addDrawable(wing.get());
    // 占位件也要统一提亮（和真模型同一套材质逻辑）
    geode->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    return geode;
}

MainWindow::MainWindow(const QString& modelFile, QWidget* parent)
    : QMainWindow(parent)
{
    // ============ ① 准备飞机模型 ============
    // 加载 → 打日志确认 → 归一化到指定半径 → 强制亮色材质
    osg::ref_ptr<osg::Node> plane;
    if (!modelFile.isEmpty())
        plane = osgDB::readNodeFile(modelFile.toStdString()); // 绝对路径
    if (!plane)
        plane = osgDB::readNodeFile("cessna.osg");            // 相对路径/OSG_FILE_PATH

    if (plane)
    {
        qDebug() << "[model] loaded, radius =" << plane->getBound().radius();
    }
    else
    {
        qWarning() << "[model] NOT loaded -> using placeholder";
        plane = makePlaceholderAircraft();
    }

    // 归一化缩放：不同模型大小差异巨大，统一缩到指定包围球半径。
    // 公里级场景里真实尺寸的目标只有一个像素点，所以态势系统惯例
    // 是把模型【夸大显示】。
    {
        const double airmodel = 1000.0;
        const double s = airmodel / plane->getBound().radius();
        osg::ref_ptr<osg::MatrixTransform> mt = new osg::MatrixTransform;
        mt->setMatrix(osg::Matrix::scale(s, s, s));
        mt->addChild(plane.get());
        plane = mt;
    }

    // 强制统一亮色材质（OVERRIDE 压过模型内部自带的暗色材质）。
    // 知识点：StateSet 沿场景图继承，加 OVERRIDE 标志后父节点
    // 强制覆盖整棵子树——不改模型文件就能统一外观。
    {
        osg::ref_ptr<osg::Material> mat = new osg::Material;
        mat->setColorMode(osg::Material::OFF);
        mat->setAmbient (osg::Material::FRONT_AND_BACK, osg::Vec4(0.35f,0.35f,0.35f,1.f));
        mat->setDiffuse (osg::Material::FRONT_AND_BACK, osg::Vec4(0.85f,0.85f,0.88f,1.f));
        mat->setEmission(osg::Material::FRONT_AND_BACK, osg::Vec4(0.45f,0.45f,0.50f,1.f)); // 自发光保底
        mat->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4(0.05f,0.05f,0.05f,1.f));
        osg::StateSet* ss = plane->getOrCreateStateSet();
        ss->setAttributeAndModes(mat.get(), osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);
    }

    // ============ ② 组装界面 ============
    QWidget* central = new QWidget(this);
    QVBoxLayout* lay = new QVBoxLayout(central);

    m_view = new OSGWidget;
    lay->addWidget(m_view, 1); // 视图占满剩余空间

    QPushButton* pauseBtn = new QPushButton(QStringLiteral("暂停"));
    pauseBtn->setCheckable(true);
    connect(pauseBtn, &QPushButton::toggled, this, &MainWindow::onPause);
    lay->addWidget(pauseBtn, 0); // 按钮固定高度

    setCentralWidget(central);
    m_view->installEventFilter(this); // ★ 监听 OSGWidget 的尺寸变化事件

    // ============ ③ 场景与视图 ============
    m_root  = new osg::Group;
    m_scene = new SituationScene(m_root.get(), plane.get());

    // ============ ★ ④ GeoJSON 行政区地图 ============
    // 把 geojson 文件放到工作目录（或改成绝对路径），替代原网格地面。
    // buildSceneEx 第二个参数 = 地图总跨度（米）：
    //   传 16000 → 地图缩放到 16km，与五角星航线（R=5km）同尺度，
    //              飞机恰好绕地图中央飞行。
    //   若你的地图是单个省/市，可按其实际东西跨度调整此值。
    QVector<geojson::GeoFeature> feats;
    QString mapErr;
    if (geojson::loadFile(QStringLiteral(GEOFILEPATH), feats, mapErr))
    {
        const geojson::MapBuildResult map = geojson::buildSceneEx(feats, 16000.0);
        m_scene->attachMap(map); // root 挂场景；regions 交给态势做入区告警+高亮
        qDebug() << "[map] loaded, regions =" << map.regions.size();
    }
    else
    {
        qWarning() << "[map]" << mapErr << "-> fallback to grid";
        m_scene->attachGrid();   // 兜底：加载失败退回原 16km 网格
    }

    m_view->setScene(m_root.get());
    m_view->setTopDownOrtho(6000.0);   // ★ 新增：正交俯视，视野半宽 6km
    m_view->setClip(10.0, 80000.0);      // 8km 场景：far 必须拉远，否则边缘被裁
    m_view->setBgColor(0.08f, 0.10f, 0.12f);

    m_radar.setNoise(80.0, 0.004);       // 测量误差：距离±80m，方位±0.23°
    m_tracker.setGain(0.65, 0.31);       // ★ 显式设定增益，方便做对比实验
    m_tracker.setGate(3000.0);

    // ============ ⑤ 仿真时钟 ============
    connect(&m_timer, &QTimer::timeout, this, &MainWindow::onTick);
    m_timer.start(33); // ~30Hz。渲染另由 OSGWidget 的 60Hz 定时器驱动
}

// 仿真节拍：唯一的职责是"推时钟 + 驱动场景"。
// 注意数据循环（30Hz）与渲染循环（60Hz）是两条独立的线，
// 渲染线程每帧画的都是当前最新的场景状态——这就是"数据与显示解耦"。
void MainWindow::onTick()
{
    m_simTime += m_dt; // 暂停时不进这里，时钟冻结

    // ① 雷达节拍：本拍可能出 0 个或 1 个点迹（扫描周期限制）
    situ::Plot plot;
    if (m_radar.step(m_simTime, plot))
    {
        m_scene->addPlot(plot);                    // 数据层 → 显示层
        //const osg::Vec3d tp = situ::truthPos(m_simTime);
        m_tracker.feed(plot);                      // 数据：起始/波门/α-β滤波
        m_scene->updateTrack(m_tracker.track());   // 显示：绿线+黄矢量+数据
    }

    // ② 飞机按真值飞（内部会做行政区入区判定）
    m_scene->updateTruth(m_simTime);
}

void MainWindow::onPause(bool on)
{
    on ? m_timer.stop() : m_timer.start(33);
}

// ------------------------------------------------------------
// Qt 事件过滤器：不改 OSGWidget 一行源码，就能"截听"它的事件。
// 返回 false = 处理完继续把事件交给目标控件（必须！返回 true
// 会吞掉事件，OSGWidget 自己就收不到 Resize 了）。
// ------------------------------------------------------------
bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == m_view && ev->type() == QEvent::Resize)
    {
        const QSize s = m_view->size();
        const qreal dpr = m_view->devicePixelRatioF();
        m_scene->setHudSize(int(s.width() * dpr), int(s.height() * dpr)); // OSG 视口用物理像素
    }
    return QMainWindow::eventFilter(obj, ev);
}
