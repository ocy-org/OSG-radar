// ============================================================
// SceneUtil —— 渲染小工具库
// 职责：把 OSG 冗长的样板代码封装成一句话工具。
// 增长计划：过程2 加动态点工具；过程3 加动态线工具；过程4 加区域标绘。
// ============================================================
#ifndef SCENEUTIL_H
#define SCENEUTIL_H

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/LineWidth>
#include <osg/Point>
#include <osg/Depth>
#include <osg/BlendFunc>
#include <osgText/Text>
#include <cmath>
#include <QDebug>
#include <cstdio>
#include <osg/Camera>
#include <osg/MatrixTransform>
#include <vector>
#include <string>
#include <osgDB/FileUtils>     // fileExists

namespace su {

// 中文字体查找：按优先级返回第一个存在的字体文件
// 顺序有讲究：simhei 是纯 ttf（最稳），msyh.ttc 是字体集合
// （FreeType 取 face 0，绝大多数 OSG 版本可读，但放在第二位）
inline const std::string& cjkFont()
{
    static std::string s_font;
    if (s_font.empty()) {
        const char* candidates[] = {
            "C:/Windows/Fonts/simhei.ttf",   // 黑体
            "C:/Windows/Fonts/msyh.ttc",     // 微软雅黑
            "C:/Windows/Fonts/simsun.ttc",   // 宋体
            "simhei.ttf", "msyh.ttc"         // 拷进工程目录的兜底
        };
        for (const char* f : candidates)
            if (osgDB::fileExists(f)) { s_font = f; break; }
        if (s_font.empty()) s_font = "fonts/arial.ttf";  // 全无→退回(无中文)
    }
    return s_font;
}

// ------------------------------------------------------------
// gridLines：以原点为中心的正方形网格（静态几何练习）
//   halfExtent : 网格半宽（米）—— total = 2*halfExtent
//   step       : 网格间距（米）
// 知识点：Geometry 四件套 = 顶点数组 + 颜色数组 + 图元 + 挂到 Geode
// ------------------------------------------------------------
inline osg::Geode* gridLines(double halfExtent, double step,
                             const osg::Vec4& color, float width = 1.f)
{
    osg::ref_ptr<osg::Geometry> g = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;   // 顶点：两个点 = 一条线
    for (double d = -halfExtent; d <= halfExtent + 1; d += step) {
        v->push_back(osg::Vec3(float(d), float(-halfExtent), 0.f)); // 平行 Y 轴的线
        v->push_back(osg::Vec3(float(d), float( halfExtent), 0.f));
        v->push_back(osg::Vec3(float(-halfExtent), float(d), 0.f)); // 平行 X 轴的线
        v->push_back(osg::Vec3(float( halfExtent), float(d), 0.f));
    }
    g->setVertexArray(v.get());
    g->addPrimitiveSet(new osg::DrawArrays(GL_LINES, 0, v->size()));

    // 颜色：一条颜色贯穿所有顶点（BIND_OVERALL）
    osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
    c->push_back(color);
    g->setColorArray(c.get(), osg::Array::BIND_OVERALL);

    // 渲染状态：网格不受光照影响（否则背光角度会变黑）
    osg::StateSet* ss = g->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    ss->setAttributeAndModes(new osg::LineWidth(width));

    osg::ref_ptr<osg::Geode> geode = new osg::Geode;  // Geode = 挂 Drawable 的叶节点
    geode->addDrawable(g.get());
    return geode.release();
}


// ------------------------------------------------------------
// makeLabel：屏幕对齐的文字标注
//   pixelSize 用像素为单位（SCREEN_COORDS）——不管相机多远，
//   字在屏幕上始终一样大。这是态势显示"信息层"的标配。
//   ★ 返回 Text 对象后，调用方负责 setPosition 和 setText。
// ------------------------------------------------------------
inline osgText::Text* makeLabel(float pixelSize, const osg::Vec4& color,
    osgText::TextBase::AlignmentType align = osgText::TextBase::LEFT_BOTTOM)
{
    osg::ref_ptr<osgText::Text> t = new osgText::Text;
    t->setCharacterSize(pixelSize);
    t->setCharacterSizeMode(osgText::TextBase::SCREEN_COORDS); // 像素大小
    t->setColor(color);
    t->setAlignment(align);
    t->setAxisAlignment(osgText::TextBase::SCREEN);      // 永远正对屏幕（billboard）
    t->setBackdropType(osgText::Text::OUTLINE);          // 描边，深色背景上更清晰
    t->setFont(cjkFont());


    osg::StateSet* ss = t->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    // 关闭深度测试：文字永远不被模型/地形遮挡（信息层优先于实景层）
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS));
    return t.release();
}

// 把 Text 包进 Geode（ osgText::Text 是 Drawable，必须挂 Geode 才能进场景图）
inline osg::Geode* labelGeode(osgText::Text* t)
{
    osg::ref_ptr<osg::Geode> g = new osg::Geode;
    g->addDrawable(t);
    g->setCullingActive(false);   // 标签不参与剔除：信息层元素任何距离都该画
    return g.release();
}


// ------------------------------------------------------------
// setupDynamic：让 Geometry 可以每帧更新的"三件套"。
// OSG 默认用显示列表缓存 GPU 命令——CPU 侧改了顶点，缓存不知道，
// 表现为"加了点但屏幕没反应"。三件套 = 改用实时 VBO 提交。
// ------------------------------------------------------------
inline void setupDynamic(osg::Geometry* g)
{
    g->setDataVariance(osg::Object::DYNAMIC);   // ① 声明：数据每帧会变
    g->setUseDisplayList(false);                // ② 关闭显示列表缓存
    g->setUseVertexBufferObjects(true);         // ③ 用 VBO，dirty() 即上传
}

// ------------------------------------------------------------
// makePoints：点迹图层（动态点集）
//   cap = 最多保留多少个点。点迹会无限到来，不设上限内存和顶点数
//   无限涨——行业做法是只留最近 N 个（FIFO），即"航迹余辉"长度。
// ------------------------------------------------------------
inline osg::Geometry* makePoints(float pixelSize, const osg::Vec4& color, unsigned cap)
{
    osg::ref_ptr<osg::Geometry> g = new osg::Geometry;
    setupDynamic(g.get());

    osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
    v->reserve(cap + 1);                        // 预分配，避免频繁扩容
    g->setVertexArray(v.get());
    g->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, 0));  // 初始 0 个点

    osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
    c->push_back(color);                        // 所有点共用一个颜色
    g->setColorArray(c.get(), osg::Array::BIND_OVERALL);

    osg::StateSet* ss = g->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    ss->setAttributeAndModes(new osg::Point(pixelSize));          // 点直径（像素）
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS)); // 信息层不被遮挡
    return g.release();
}

// ------------------------------------------------------------
// appendPoint：尾部加点，超 cap 从头部淘汰（FIFO）。
//   改完必须 dirty()，否则数据只在 CPU 内存里，GPU 看不见。
// ------------------------------------------------------------
inline void appendPoint(osg::Geometry* g, const osg::Vec3& p, unsigned cap)
{
    auto* v = static_cast<osg::Vec3Array*>(g->getVertexArray());
    if (v->size() >= cap)
        v->erase(v->begin());                   // 淘汰最老的点

    v->push_back(p);

    // 顶点数量变了 → 图元描述同步更新
    if (auto* da = dynamic_cast<osg::DrawArrays*>(g->getPrimitiveSet(0)))
        da->set(GL_POINTS, 0, GLsizei(v->size()));

    v->dirty();                                 // ★ 通知 OSG 重新上传
}

// ------------------------------------------------------------
// rewritePoint：就地改第 i 个顶点
// ------------------------------------------------------------
inline void rewritePoint(osg::Geometry* g, unsigned i, const osg::Vec3& p)
{
    auto* v = static_cast<osg::Vec3Array*>(g->getVertexArray());
    if (i < v->size()) { (*v)[i] = p; v->dirty(); }
}

// ============================================================
// 区域标绘工具
// circleFan：扇形/圆盘【填充面】（GL_TRIANGLE_FAN）
//   center 圆心(世界坐标)  radius 半径(米)
//   a0/a1  起止方位角（弧度，约定：0°=+X东、逆时针为正，与 atan2 一致）
//   segs   弧线细分段数（越大越圆滑）
// 知识点1 GL_TRIANGLE_FAN：第 0 个顶点是公共顶点(扇心)，
//   (C,P0,P1)(C,P1,P2)... 依次生成三角形 —— 天生适合扇形/圆盘，
//   n 个顶点出 n-2 个三角形，无重复顶点、无索引开销。
// 知识点2 半透明渲染三原则（见下方注释逐条对应）
// ============================================================
inline osg::Geometry* circleFan(const osg::Vec3& center, float radius,
                                float a0, float a1, int segs,
                                const osg::Vec4& color)
{
    osg::ref_ptr<osg::Geometry> g = new osg::Geometry;
    setupDynamic(g.get());   // 告警联动要改写颜色 → 必须动态几何

    // 顶点 = 扇心 + 弧上 segs+1 个点（a0 到 a1 首尾都含）
    osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
    v->reserve(segs + 2);
    v->push_back(center);
    for (int i = 0; i <= segs; ++i) {
        const float a = a0 + (a1 - a0) * (float(i) / float(segs));
        v->push_back(center + osg::Vec3(radius * std::cos(a),
                                        radius * std::sin(a), 0.f));
    }
    g->setVertexArray(v.get());
    g->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLE_FAN, 0, v->size()));

    osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
    c->push_back(color);
    g->setColorArray(c.get(), osg::Array::BIND_OVERALL);

    osg::StateSet* ss = g->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

    // —— 半透明三原则 ——
    // ① 开混合。公式：结果 = 源色×α + 目标色×(1−α)
    //    α=0.22 → 22%的橙 + 78%的背景 = 一层"淡橙纱"
    ss->setMode(GL_BLEND, osg::StateAttribute::ON);
    ss->setAttributeAndModes(
        new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    // ② 深度【测试】保留（不遮挡更近的已画物体），
    //    深度【写入】关闭 —— 透明面若写深度，会挡住其后绘制的
    //    透明片元，混合顺序错乱（实验2会让你亲眼看到这个 bug）
    {
        osg::ref_ptr<osg::Depth> d = new osg::Depth(osg::Depth::LESS);
        d->setWriteMask(false);
        ss->setAttributeAndModes(d.get());
    }
    // ③ 登记进透明渲染队列：OSG 保证先画全部不透明物体，
    //    再按距离由远及近画透明物体 —— 透明排序问题的标准解法
    ss->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
    return g.release();
}

// ------------------------------------------------------------
// circleLine：圆弧/圆环【轮廓线】
//   withCenter=false → GL_LINE_STRIP 纯弧线（a1−a0=2π 时视觉闭合为整圆）
//   withCenter=true  → GL_LINE_LOOP：顶点序列 [圆心, 弧...] 自动闭合，
//                      一条命令同时画出 弧线+两条半径边，正适合扇形轮廓
// 填充用半透明、轮廓用实线 —— 这是标绘制图的标准配色惯例
// ------------------------------------------------------------
inline osg::Geometry* circleLine(const osg::Vec3& center, float radius,
                                 float a0, float a1, int segs,
                                 const osg::Vec4& color, float width,
                                 bool withCenter = false)
{
    osg::ref_ptr<osg::Geometry> g = new osg::Geometry;

    osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
    if (withCenter) v->push_back(center);
    for (int i = 0; i <= segs; ++i) {
        const float a = a0 + (a1 - a0) * (float(i) / float(segs));
        v->push_back(center + osg::Vec3(radius * std::cos(a),
                                        radius * std::sin(a), 0.f));
    }
    g->setVertexArray(v.get());
    g->addPrimitiveSet(new osg::DrawArrays(
        withCenter ? GL_LINE_LOOP : GL_LINE_STRIP, 0, v->size()));

    osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
    c->push_back(color);
    g->setColorArray(c.get(), osg::Array::BIND_OVERALL);

    osg::StateSet* ss = g->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    ss->setAttributeAndModes(new osg::LineWidth(width));
    return g.release();
}

// ------------------------------------------------------------
// azimuthRuler：方位刻度盘 —— 刻度短线 + 外圈整圆 + 角度数字
// 标注沿圆周布点：还是圆参数方程（本工程第三次用它：
//   starVertex 算航线 → RadarSim 算真值 → 这里摆标注）
// 度数用数学约定（0°=东、逆时针）。真实雷达屏常用"0°=北、顺时针"，
// 两套约定换算：radarDeg = (90 − mathDeg + 360) % 360
// ------------------------------------------------------------
inline osg::Geode* azimuthRuler(double r0, double r1, double rLab, float z,
                                const osg::Vec4& color
                                = osg::Vec4(0.45f, 0.50f, 0.58f, 1.f))
{
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;

    // a) 刻度短线：每 30° 一条（GL_LINES：24 顶点 = 12 条线）
    {
        osg::ref_ptr<osg::Geometry> g = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
        for (int d = 0; d < 360; d += 30) {
            const double a = d * osg::PI / 180.0;        // 度 → 弧度
            v->push_back(osg::Vec3(float(r0*std::cos(a)), float(r0*std::sin(a)), z));
            v->push_back(osg::Vec3(float(r1*std::cos(a)), float(r1*std::sin(a)), z));
        }
        g->setVertexArray(v.get());
        g->addPrimitiveSet(new osg::DrawArrays(GL_LINES, 0, v->size()));
        osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
        c->push_back(color);
        g->setColorArray(c.get(), osg::Array::BIND_OVERALL);
        osg::StateSet* ss = g->getOrCreateStateSet();
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        ss->setAttributeAndModes(new osg::LineWidth(1.5f));
        geode->addDrawable(g.get());
    }
    // b) 外圈整圆（复用 circleLine：0 → 2π）
    geode->addDrawable(circleLine(osg::Vec3(0, 0, z), float(r1),
                                  0.f, float(2.0 * osg::PI), 72, color, 1.5f));
    // c) 角度数字标注（Text 也是 Drawable，可多个共存一个 Geode）
    for (int d = 0; d < 360; d += 30) {
        const double a = d * osg::PI / 180.0;
        char buf[8];
        std::snprintf(buf, sizeof buf, "%d", d);
        osg::ref_ptr<osgText::Text> t =
            makeLabel(12.f, color, osgText::TextBase::CENTER_CENTER);
        t->setText(buf);
        t->setPosition(osg::Vec3(float(rLab * std::cos(a)),
                                 float(rLab * std::sin(a)), z));
        geode->addDrawable(t.get());
    }
    return geode.release();
}



// ============================================================
// 动态折线工具
// ============================================================
// makeLine：动态折线（GL_LINE_STRIP），cap = 最大顶点数
inline osg::Geometry* makeLine(float width, const osg::Vec4& color, unsigned cap)
{
    osg::ref_ptr<osg::Geometry> g = new osg::Geometry;
    setupDynamic(g.get());                       // 复用过程2的三件套

    osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
    v->reserve(cap + 1);
    g->setVertexArray(v.get());
    g->addPrimitiveSet(new osg::DrawArrays(GL_LINE_STRIP, 0, 0)); // 初始0顶点

    osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
    c->push_back(color);
    g->setColorArray(c.get(), osg::Array::BIND_OVERALL);

    osg::StateSet* ss = g->getOrCreateStateSet();
    ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    ss->setAttributeAndModes(new osg::LineWidth(width));
    ss->setAttributeAndModes(new osg::Depth(osg::Depth::ALWAYS)); // 信息层不被遮挡
    return g.release();
}

// appendLinePoint：折线尾部加点（与 appendPoint 同一套 FIFO，
// 区别只在图元模式是 GL_LINE_STRIP）
inline void appendLinePoint(osg::Geometry* g, const osg::Vec3& p, unsigned cap)
{
    auto* v = static_cast<osg::Vec3Array*>(g->getVertexArray());
    if (v->size() >= cap) v->erase(v->begin());
    v->push_back(p);
    if (auto* da = dynamic_cast<osg::DrawArrays*>(g->getPrimitiveSet(0)))
        da->set(GL_LINE_STRIP, 0, GLsizei(v->size()));
    v->dirty();
}

// ------------------------------------------------------------
// makeHudCamera：HUD 告警面板相机。所谓 HUD，就是无论三维场景怎么变，
// 它都按【屏幕像素坐标】显示的二维子场景（战斗机抬头显示的由来）。
// 五要素缺一即翻车（症状见括号）：
// ① 正交投影 ortho2D(0,w,0,h)：1 单位 = 1 像素（不定则坐标错乱）
// ② ABSOLUTE_RF 绝对参考帧：不受任何父变换影响（不定则跟着场景跑）
// ③ 视图矩阵 identity：不受主相机漫游影响（不定则转个视角就飞）
// ④ 只清深度缓冲：不清颜色（清了会把三维场景整个擦掉！）
// ⑤ POST_RENDER 最后绘制：永远盖在三维画面上（不定则被场景盖住）
// ------------------------------------------------------------
inline osg::Camera* makeHudCamera()
{
    osg::ref_ptr<osg::Camera> cam = new osg::Camera;
    cam->setProjectionMatrix(osg::Matrix::ortho2D(0.0, 1280.0, 0.0, 800.0)); // 初值，setSize 修正
    cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    cam->setViewMatrix(osg::Matrix::identity());
    cam->setClearMask(GL_DEPTH_BUFFER_BIT);
    cam->setRenderOrder(osg::Camera::POST_RENDER);
    cam->setAllowEventFocus(false);            // HUD 不抢鼠标键盘
    return cam.release();
}

// ------------------------------------------------------------
// HudAlertPanel：右上角告警列表。
//   固定 N 个 Text 槽位 + 队列：新告警插到最上，旧行逐行下移，
//   超过 N 条丢弃最老 —— 点迹 FIFO 的同一思想，"容器是文字行"。
//   槽位数量固定、内容每条改写 —— 速度矢量"定长改写"模式的复用：
//   不 new/delete Text 对象，只在既有对象上 setText/setColor。
// ------------------------------------------------------------
class HudAlertPanel
{
public:
    explicit HudAlertPanel(int maxLines = 6)
        : m_maxLines(maxLines)
    {
        m_cam = makeHudCamera();
        m_anchor = new osg::MatrixTransform;    // 右上角锚点

        // 面板尺寸：宽按最长文案估（约28字符×6.5px），加字段要同步加宽 W
        const float W = 232.f;
        const float H = 18.f * (maxLines + 1) + 16.f;   // 标题+N行+上下留白
        const float Z = -0.1f;                  // 与文字错开（深度已关，双保险）

        // ① 背板（bin 8，最先画 = 垫底）。四边形用过程4的 TRIANGLE_FAN：4点=2三角
        {
            osg::ref_ptr<osg::Geometry> bg = new osg::Geometry;
            osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
            v->push_back(osg::Vec3(-W, 10.f - H, Z));   // 左下（原点=右上角锚点）
            v->push_back(osg::Vec3(  6, 10.f - H, Z));  // 右下
            v->push_back(osg::Vec3(  6,      10.f, Z)); // 右上
            v->push_back(osg::Vec3(-W,      10.f, Z));  // 左上
            bg->setVertexArray(v.get());
            bg->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLE_FAN, 0, 4));
            osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
            //c->push_back(osg::Vec4(0.04f, 0.06f, 0.09f, 0.78f)); // 深蓝黑 78%不透明
            // ① 背板颜色：近黑 → 浅灰（α 提到 0.92，面板感更实）
            c->push_back(osg::Vec4(0.88f, 0.90f, 0.93f, 0.92f));   // 原 (0.04,0.06,0.09,0.78)
            bg->setColorArray(c.get(), osg::Array::BIND_OVERALL);
            osg::StateSet* ss = bg->getOrCreateStateSet();
            ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
            ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
            ss->setMode(GL_BLEND, osg::StateAttribute::ON);
            ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
            ss->setRenderBinDetails(8, "RenderBin");    // ★ 决定先画
            osg::ref_ptr<osg::Geode> g = new osg::Geode;
            g->addDrawable(bg.get());
            m_anchor->addChild(g.get());
        }
        // ② 边框（bin 9）
        {
            osg::ref_ptr<osg::Geometry> bd = new osg::Geometry;
            osg::ref_ptr<osg::Vec3Array> v = new osg::Vec3Array;
            v->push_back(osg::Vec3(-W, 10.f - H, Z));
            v->push_back(osg::Vec3(  6, 10.f - H, Z));
            v->push_back(osg::Vec3(  6,      10.f, Z));
            v->push_back(osg::Vec3(-W,      10.f, Z));
            bd->setVertexArray(v.get());
            bd->addPrimitiveSet(new osg::DrawArrays(GL_LINE_LOOP, 0, 4));
            osg::ref_ptr<osg::Vec4Array> c = new osg::Vec4Array;
            //c->push_back(osg::Vec4(0.45f, 0.60f, 0.80f, 0.9f));
            // ② 边框颜色：亮蓝灰 → 深蓝灰（浅底上亮边框会"融化"）
            c->push_back(osg::Vec4(0.30f, 0.42f, 0.58f, 1.f));     // 原 (0.45,0.60,0.80,0.9)
            bd->setColorArray(c.get(), osg::Array::BIND_OVERALL);
            osg::StateSet* ss = bd->getOrCreateStateSet();
            ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
            ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
            ss->setAttributeAndModes(new osg::LineWidth(1.5f));
            ss->setRenderBinDetails(9, "RenderBin");
            osg::ref_ptr<osg::Geode> g = new osg::Geode;
            g->addDrawable(bd.get());
            m_anchor->addChild(g.get());
        }

        // ③ 文字（默认 bin 10，最后画 = 最上层），每条都加深色描边
        osg::ref_ptr<osg::Geode> textGeode = new osg::Geode;
        //m_title = su::makeLabel(13.f, osg::Vec4(0.85f, 0.92f, 1.f, 1.f),
        //                        osgText::TextBase::RIGHT_TOP);
        // 改后：红色标题
        m_title = su::makeLabel(13.f, osg::Vec4(0.85f, 0.08f, 0.08f, 1.f),
                                osgText::TextBase::RIGHT_TOP);
        m_title->setText("==告警信息==", osgText::String::ENCODING_UTF8);

        hudTextPost(m_title.get());
        textGeode->addDrawable(m_title.get());

        m_slots.reserve(maxLines);
        for (int i = 0; i < maxLines; ++i) {
            osg::ref_ptr<osgText::Text> t =
                su::makeLabel(12.f, osg::Vec4(1, 1, 1, 1),
                              osgText::TextBase::RIGHT_TOP);
            t->setText("");
            t->setPosition(osg::Vec3(0.f, -float(i + 1) * 18.f, 0.f));
            hudTextPost(t.get());
            textGeode->addDrawable(t.get());
            m_slots.push_back(t);
        }
        m_anchor->addChild(textGeode.get());

        m_cam->addChild(m_anchor.get());
        setSize(1280, 800);
    }

    osg::Camera* camera() const { return m_cam.get(); }

    void setSize(int w, int h)   // 与 v1 相同
    {
        m_cam->setProjectionMatrix(osg::Matrix::ortho2D(0.0, double(w), 0.0, double(h)));
        m_cam->setViewport(0, 0, w, h);
        m_anchor->setMatrix(osg::Matrix::translate(double(w) - 24.0, double(h) - 40.0, 0));
    }

    void push(const std::string& msg, const osg::Vec4& color)   // 与 v1 相同
    {
        m_items.insert(m_items.begin(), Item{msg, color});
        if (int(m_items.size()) > m_maxLines) m_items.pop_back();
        refresh();
    }

private:
    struct Item { std::string msg; osg::Vec4 color; };

    void refresh()   // 与 v1 相同
    {
        for (int i = 0; i < m_maxLines; ++i) {
            if (i < int(m_items.size())) {
                //m_slots[i]->setText(m_items[i].msg);
                m_slots[i]->setText(m_items[i].msg, osgText::String::ENCODING_UTF8);
                m_slots[i]->setColor(m_items[i].color);
            } else m_slots[i]->setText("");
        }
    }

    // osgText 自带描边：沿字形画一圈深色轮廓，字在任何背景上都有对比度
    static void hudTextPost(osgText::Text* t)
    {
        t->setBackdropType(osgText::Text::NONE);   // 原 OUTLINE
//        t->setBackdropType(osgText::Text::OUTLINE);
//        t->setBackdropColor(osg::Vec4(0.f, 0.f, 0.f, 0.9f));
//        t->setBackdropOffset(1.2f);
    }

    osg::ref_ptr<osg::Camera>                m_cam;
    osg::ref_ptr<osg::MatrixTransform>       m_anchor;
    osg::ref_ptr<osgText::Text>              m_title;
    std::vector<osg::ref_ptr<osgText::Text>> m_slots;
    std::vector<Item>                        m_items;
    int                                      m_maxLines;
};

} // namespace su
#endif
