#include "situationscene.h"
#include "SceneUtil.h"

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/LineWidth>
#include <osg/Point>
#include <osg/ShapeDrawable>
#include <osg/Matrix>
#include <osg/Material>
#include <osg/Depth>
#include <cmath>
#include <cstdio>

static const double kVelLead = 5.0; // 5秒×1585m/s≈7.9km，16km网格上长度合适

// 区域告警配色（原扇形/禁飞区的 kWarn*/kNoFly* 常量已删除）
// 高亮色取不透明亮红：地图填充层本来就不透明，用不透明高亮
// 可以避免给整个填充层开混合+透明排序，简单可靠。
static const osg::Vec4 kRegionHot (1.0f, 0.15f, 0.08f, 1.0f); // 入区高亮：亮红
static const osg::Vec4 kAlertIn   (0.78f, 0.05f, 0.05f, 1.f); // 入界告警：深红
static const osg::Vec4 kAlertOut  (0.62f, 0.33f, 0.00f, 1.f); // 越界告警：深橙

SituationScene::SituationScene(osg::Group* root, osg::Node* model)
    : m_root(root)
{
    // ============ ★ 优化1：隐藏蓝色五角星航线 ============
    // 不再调用 buildStarRoute()，青色航线线 + 顶点标记都不会创建。
    // 函数体保留在文件里，以后想恢复把下面这行的注释去掉即可：
    // buildStarRoute();

    buildPlots();
    buildTracks();
    buildHud();     // HUD 告警面板
    buildAircraft();

    if (model)
        m_airMt->addChild(model); // 飞机模型挂到变换节点下
    m_root->addChild(m_airMt.get());
}

// ============================================================
// 挂载 GeoJSON 行政区地图
//   root 直接 addChild（内部已分好 填充/描边/线/点/标签 五个图层）
//   regions 存下来 + 初始化"上一帧状态"为 false（都不在区内）
// ============================================================
void SituationScene::attachMap(const geojson::MapBuildResult& map)
{
    if (map.root.valid())
        m_root->addChild(map.root.get());

    m_regions  = map.regions;
    m_regionIn = QVector<bool>(m_regions.size(), false);
}

// 兜底：GeoJSON 加载失败时还原原 16km 网格地面（保持程序可跑）
void SituationScene::attachGrid()
{
    m_root->addChild(su::gridLines(8000.0, 1000.0,
                                   osg::Vec4(0.55f, 0.58f, 0.62f, 1.f), 1.f));
}

// ============================================================
// 五角星航线可视化（★ 优化1：已不再调用，保留备查）
// ============================================================
void SituationScene::buildStarRoute()
{
    // a) 航线折线
    {
        osg::ref_ptr<osg::Geometry> g = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
        for (int i = 0; i < 5; ++i)  // LOOP 模式自动回到起点闭合
            v->push_back(osg::Vec3(situ::starVertex(situ::starSeq(i))));
        g->setVertexArray(v.get());
        g->addPrimitiveSet(new osg::DrawArrays(GL_LINE_LOOP, 0, v->size()));

        osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
        c->push_back(osg::Vec4(0.2f, 0.9f, 1.f, 1.f)); // 青色
        g->setColorArray(c.get(), osg::Array::BIND_OVERALL);

        osg::StateSet* ss = g->getOrCreateStateSet();
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        ss->setAttributeAndModes(new osg::LineWidth(2.5f));

        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(g.get());
        m_root->addChild(geode.get());
    }

    // b) 顶点标记
    {
        osg::ref_ptr<osg::Geometry> g = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
        for (int i = 0; i < 5; ++i)
            v->push_back(osg::Vec3(situ::starVertex(i)));
        g->setVertexArray(v.get());
        g->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, v->size()));

        osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
        c->push_back(osg::Vec4(0.2f, 0.9f, 1.f, 1.f));
        g->setColorArray(c.get(), osg::Array::BIND_OVERALL);

        osg::StateSet* ss = g->getOrCreateStateSet();
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        ss->setAttributeAndModes(new osg::Point(10.f));

        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(g.get());
        m_root->addChild(geode.get());
    }
}

// ============================================================
// 点迹
// ============================================================
void SituationScene::buildPlots()
{
    // 6 像素红点，上限 240 个。返回的 Geometry 是"活的"：
    // 之后每来一个点迹就 appendPoint 一次，图形自动增长
    m_plotGeom = su::makePoints(6.f, osg::Vec4(1.f, 0.25f, 0.2f, 1.f), m_plotCap);

    osg::ref_ptr<osg::Geode> geode = new osg::Geode;
    geode->addDrawable(m_plotGeom.get());
    m_root->addChild(geode.get());
}

void SituationScene::addPlot(const situ::Plot& plot)
{
    // 极坐标 → 直角坐标（数据层的换算函数，z=0）
    const osg::Vec3d p = situ::polarToWorld(plot);

    // z 强制 0.6：二坐标雷达测不到高度，点迹只能落在地面。
    // 所以红点云贴地、飞机在 800m 高空——两者之间靠白色垂线联系。
    su::appendPoint(m_plotGeom.get(),
                    osg::Vec3(float(p.x()), float(p.y()), 0.6f),
                    m_plotCap);
}

// ============================================================
// 航迹
// ============================================================
void SituationScene::buildTracks()
{
    m_trkLine = su::makeLine(2.5f, osg::Vec4(0.25f, 1.f, 0.4f, 1.f), m_trkCap);
    osg::ref_ptr<osg::Geode> g1 = new osg::Geode;
    g1->addDrawable(m_trkLine.get());
    m_root->addChild(g1.get());

    m_velLine = su::makeLine(2.5f, osg::Vec4(1.f, 0.8f, 0.1f, 1.f), 2);
    osg::ref_ptr<osg::Geode> g2 = new osg::Geode;
    g2->addDrawable(m_velLine.get());
    m_root->addChild(g2.get());

    m_trkLabel = su::makeLabel(14.f, osg::Vec4(0.55f, 1.f, 0.65f, 1.f));
    m_trkLabel->setText("T01");
    m_root->addChild(su::labelGeode(m_trkLabel.get())); // 挂根节点 → 世界坐标
}

void SituationScene::updateTrack(const situ::Track& trk)
{
    if (!trk.valid)
        return;

    // 航迹在地面平面（雷达测不到高度）；z 抬 0.3 避免与地图填充面重叠
    const osg::Vec3 p3(float(trk.pos.x()), float(trk.pos.y()), 0.3f);
    const osg::Vec3 v3(float(trk.vel.x()), float(trk.vel.y()), 0.f);

    // ① 航迹历史线：追加滤波位置（增长式）
    su::appendLinePoint(m_trkLine.get(), p3, m_trkCap);

    // ② 速度矢量：当前位置 → 5 秒后预测位置（定长 2 顶点改写式）
    const osg::Vec3 tip = p3 + v3 * float(kVelLead);
    auto* v = static_cast<osg::Vec3Array*>(m_velLine->getVertexArray());
    if (v->size() < 2)
    {
        v->push_back(p3);
        v->push_back(tip);
        if (auto* da = dynamic_cast<osg::DrawArrays*>(m_velLine->getPrimitiveSet(0)))
            da->set(GL_LINE_STRIP, 0, 2);
        v->dirty();
    }
    else
    {
        su::rewritePoint(m_velLine.get(), 0, p3);
        su::rewritePoint(m_velLine.get(), 1, tip);
    }

    // ③ 数据块：T01 + 速度
    char buf[64];
    std::snprintf(buf, sizeof buf, "T01 %.0f m/s", trk.vel.length());
    m_trkLabel->setText(buf);
    m_trkLabel->setPosition(p3 + osg::Vec3(0.f, 0.f, 120.f));
}

// ============================================================
// ★ 行政区入区判定 + 边沿告警 + 区域高亮 + 机载标签刷新
// 每帧都查（30Hz）——飞机位置每帧都变，区域判断必须同步。
// ============================================================
void SituationScene::updateMapZones(const osg::Vec3d& p, double t)
{
    // ★ 优化3：先记录本帧飞机所在区域（可能为空），循环里顺便填
    const geojson::MapRegion* curRegion = nullptr;

    if (!m_regions.isEmpty())
    {
        char buf[160];
        for (int i = 0; i < m_regions.size(); ++i)
        {
            const geojson::MapRegion& r = m_regions[i];
            const bool in = geojson::pointInRegion(p, r); // 射线法判定

            // ============ 边沿检测 → 告警事件 ============
            // 与上一帧状态比较、只在跳变瞬间上报（边沿触发）：
            // 一次进入恰好一条 ENTER、一次离开恰好一条 EXIT。
            if (in != m_regionIn[i])
            {
                // 区域名可能含中文：toUtf8 转字节流，
                // HUD 内部 setText 已按 ENCODING_UTF8 解析
                std::snprintf(buf, sizeof buf, "[%5.1fs] T01 %s %s",
                              t, in ? "ENTER" : "EXIT",
                              r.name.toUtf8().constData());
                m_hud.push(buf, in ? kAlertIn : kAlertOut);

                // 区域显亮：改写该区域全部填充几何体的颜色数组。
                // 一个 Feature 可能是 MultiPolygon → 有多个填充体，逐个处理。
                for (const auto& g : r.fillGeoms)
                {
                    if (!g.valid())
                        continue;
                    auto* c = static_cast<osg::Vec4Array*>(g->getColorArray());
                    if (!c || c->empty())
                        continue;
                    (*c)[0] = in ? kRegionHot : r.baseColor; // 进入亮红/离开恢复
                    c->dirty();  // 必须 dirty()，否则只改了 CPU 内存，GPU 看不见
                }

                m_regionIn[i] = in; // 存为"上一帧"，供下一帧比较
            }

            // ★ 优化3：记录飞机当前所在区域（取第一个命中的即可；
            //   行政区互不重叠，正常情况下至多命中一个）
            if (in && !curRegion)
                curRegion = &m_regions[i];
        }
    }

    // ============ ★ 优化3：机载标签实时显示所在区域名 ============
    // 与缓存 m_curRegion 比较，只在变化时 setText：
    //   a) 避免每帧重建文字纹理（30Hz 下无谓开销）；
    //   b) 不在区域上空时显示默认的 TRUTH。
    const QString curName = curRegion ? curRegion->name : QString();
    if (curName != m_curRegion)
    {
        m_curRegion = curName;
        if (m_airLabel.valid())
        {
            const std::string txt = curName.isEmpty()
                                        ? std::string("TRUTH")
                                        : curName.toStdString();
            // 中文必须显式声明 UTF-8 编码，否则按 Latin1 解析成乱码
            m_airLabel->setText(osgText::String(txt,
                                                osgText::String::ENCODING_UTF8));
            // 在区域内时标签同步变红，与区域高亮呼应；区外恢复默认色
            m_airLabel->setColor(curRegion
                                     ? osg::Vec4(1.0f, 0.30f, 0.22f, 1.f)
                                     : osg::Vec4(0.85f, 0.95f, 1.f, 1.f));
        }
    }
}

// ------------------------------------------------------------
// HUD 告警面板：屏幕右上角（POST_RENDER 独立相机，非场景物体）
// ------------------------------------------------------------
void SituationScene::buildHud()
{
    m_root->addChild(m_hud.camera());
}

void SituationScene::setHudSize(int w, int h)
{
    m_hud.setSize(w, h);
}

// ------------------------------------------------------------
// 飞机 + 机载标签 + 垂直下引线
//
// 场景图坐标规则：
// 挂在 MatrixTransform 下的子节点用【本地坐标】——
// 标签只设一次本地偏移 (0,0,300)，不要在 update 里再叠世界坐标。
// ------------------------------------------------------------
void SituationScene::buildAircraft()
{
    // 飞机变换节点：动画唯一的作用对象
    m_airMt = new osg::MatrixTransform;

    // a) 机载标签：本地坐标，飞机上方 300m
    // ★ 优化3：标签文字要显示中文区县名 —— 必须指定中文字体，
    //   否则 osgText 默认字体没有 CJK 字形，中文会显示成方框
    m_airLabel = su::makeLabel(15.f, osg::Vec4(0.85f, 0.95f, 1.f, 1.f));
    m_airLabel->setText("TRUTH");
    const QString cjkFont = geojson::chineseFont();   // ★ 取本机中文字体
    if (!cjkFont.isEmpty())
        m_airLabel->setFont(cjkFont.toStdString());
    m_airLabel->setPosition(osg::Vec3(0.f, 0.f, 300.f)); // 本地偏移，只设这一次
    m_airMt->addChild(su::labelGeode(m_airLabel.get()));

    // b) 垂直下引线：本地 (0,0,0) → (0,0,-800)
    {
        osg::ref_ptr<osg::Geometry> g = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
        v->push_back(osg::Vec3(0.f, 0.f,    0.f));   // 端点0：飞机脚下
        v->push_back(osg::Vec3(0.f, 0.f, -800.f));   // 端点1：地面投影
        g->setVertexArray(v.get());
        g->addPrimitiveSet(new osg::DrawArrays(GL_LINES, 0, 2));

        osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
        c->push_back(osg::Vec4(1.f, 1.f, 1.f, 0.75f));
        g->setColorArray(c.get(), osg::Array::BIND_OVERALL);

        osg::StateSet* ss = g->getOrCreateStateSet();
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        ss->setAttributeAndModes(new osg::LineWidth(2.f));
        ss->setMode(GL_BLEND, osg::StateAttribute::ON);
        ss->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS));

        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(g.get());
        m_airMt->addChild(geode.get());
    }
}

// ------------------------------------------------------------
// 每帧驱动："调数据层 + 摆矩阵"，飞机按真值飞（数学在数据层）
// ------------------------------------------------------------
void SituationScene::updateTruth(double t)
{
    const osg::Vec3d pos = situ::truthPos(t);
    const osg::Vec3d dir = situ::truthVel(t);

    osg::Quat rot;
    rot.makeRotate(osg::Vec3d(0, -1, 0), dir);
    m_airMt->setMatrix(osg::Matrix::rotate(rot) * osg::Matrix::translate(pos));

    updateMapZones(pos, t); // 入区判定 + 告警 + 区域显亮 + 标签刷新
}
