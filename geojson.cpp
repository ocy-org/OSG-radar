#include "geojson.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QStringList>
#include <QtMath>

#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/LineWidth>
#include <osg/Point>
#include <osg/StateSet>
#include <osgText/Text>
#include <osgUtil/Tessellator>

#include <cmath>

namespace geojson {

// ================================================================
// ① 解析：GeoJSON 文本 → 数据模型（每一层都做防御性访问）
// ================================================================
static GeoRing parseRing(const QJsonArray& ringArr)
{
    GeoRing ring;
    ring.coords.reserve(ringArr.size());
    for (const QJsonValue& pv : ringArr)
    {
        const QJsonArray pt = pv.toArray();
        if (pt.size() >= 2)
        {
            GeoCoord c;
            c.lon = pt.at(0).toDouble(); // GeoJSON 顺序：[经度, 纬度]
            c.lat = pt.at(1).toDouble();
            ring.coords.append(c);
        }
    }
    return ring;
}

static GeoPolygonData parsePolygonCoords(const QJsonArray& coordsArr)
{
    GeoPolygonData poly;
    for (const QJsonValue& rv : coordsArr)
        poly.rings.append(parseRing(rv.toArray()));
    return poly;
}

bool loadFile(const QString& path, QVector<GeoFeature>& outFeatures, QString& err)
{
    outFeatures.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        err = QObject::tr("无法打开文件：%1").arg(path);
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseErr);
    if (parseErr.error != QJsonParseError::NoError)
    {
        err = QObject::tr("JSON 解析失败：%1（偏移 %2 字节）")
                  .arg(parseErr.errorString()).arg(parseErr.offset);
        return false;
    }
    if (!doc.isObject())
    {
        err = QObject::tr("JSON 根节点不是对象");
        return false;
    }

    const QJsonObject root = doc.object();
    if (root.value("type").toString() != "FeatureCollection")
    {
        err = QObject::tr("不支持的类型：%1（本例只处理 FeatureCollection）")
                  .arg(root.value("type").toString());
        return false;
    }

    const QJsonArray features = root.value("features").toArray();
    for (const QJsonValue& fv : features)
    {
        const QJsonObject feature = fv.toObject();
        const QJsonObject geom    = feature.value("geometry").toObject();
        const QJsonObject props   = feature.value("properties").toObject();

        GeoFeature gf;
        gf.geomType = geom.value("type").toString();
        gf.name     = props.value("name").toString();
        if (gf.name.isEmpty())
            gf.name = props.value("NAME").toString(); // 有些数据集用大写字段

        const QJsonArray coords = geom.value("coordinates").toArray();

        if (gf.geomType == "Polygon")
        {
            gf.polygons.append(parsePolygonCoords(coords));
        }
        else if (gf.geomType == "MultiPolygon")
        {
            for (const QJsonValue& pv : coords)
                gf.polygons.append(parsePolygonCoords(pv.toArray()));
        }
        else if (gf.geomType == "LineString")
        {
            gf.lines.append(parseRing(coords).coords);
        }
        else if (gf.geomType == "MultiLineString")
        {
            for (const QJsonValue& lv : coords)
                gf.lines.append(parseRing(lv.toArray()).coords);
        }
        else if (gf.geomType == "Point")
        {
            if (coords.size() >= 2)
                gf.points.append(GeoCoord{ coords.at(0).toDouble(),
                                           coords.at(1).toDouble() });
        }
        else if (gf.geomType == "MultiPoint")
        {
            for (const QJsonValue& pv : coords)
            {
                const QJsonArray pt = pv.toArray();
                if (pt.size() >= 2)
                    gf.points.append(GeoCoord{ pt.at(0).toDouble(),
                                               pt.at(1).toDouble() });
            }
        }

        outFeatures.append(gf);
    }
    return true;
}

// ================================================================
// ② 投影：经纬度 → 以数据包围盒中心为原点的局部米制坐标
// ================================================================
static const double kEarthR = 6378137.0; // WGS84 赤道半径（米）

// 自定义 POD 代替 QPointF：少一个 Qt 头文件依赖
struct MeterXY { double x; double y; };

static MeterXY projectMeters(const GeoCoord& c, const GeoCoord& center)
{
    MeterXY m;
    // 经度方向：纬线圈随纬度收缩，乘 cos(中心纬度) 修正
    m.x = qDegreesToRadians(c.lon - center.lon) * kEarthR
          * std::cos(qDegreesToRadians(center.lat));
    m.y = qDegreesToRadians(c.lat - center.lat) * kEarthR;
    return m;
}

template <typename F>
static void forEachCoord(const QVector<GeoFeature>& features, F&& fn)
{
    for (const GeoFeature& f : features)
    {
        for (const GeoPolygonData& p : f.polygons)
            for (const GeoRing& r : p.rings)
                for (const GeoCoord& c : r.coords)
                    fn(c);
        for (const QVector<GeoCoord>& l : f.lines)
            for (const GeoCoord& c : l)
                fn(c);
        for (const GeoCoord& c : f.points)
            fn(c);
    }
}

// 找一个支持中文的字体（osgText 默认字体没有 CJK 字形，中文会变方框）
static QString findChineseFont()
{
    static const QStringList candidates = {
        "C:/Windows/Fonts/msyh.ttc",   // 微软雅黑
        "C:/Windows/Fonts/simhei.ttf", // 黑体
        "C:/Windows/Fonts/simsun.ttc", // 宋体
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/System/Library/Fonts/PingFang.ttc",
    };
    for (const QString& p : candidates)
        if (QFile::exists(p))
            return p;
    return QString();
}

// ★ 对外暴露中文字体查找结果（供机载标签等其他 osgText 使用）
QString chineseFont()
{
    return findChineseFont();
}

// ================================================================
// ★ 点在环内判断（射线法 / 偶奇规则）
// 只用 xy 分量：雷达世界是二维的，飞机 z 不参与判断
// ================================================================
static bool pointInRing(const osg::Vec3d& p, const QVector<osg::Vec3>& ring)
{
    bool inside = false;
    const int n = ring.size();
    for (int i = 0, j = n - 1; i < n; j = i++) // j 始终是 i 的前一个顶点
    {
        const osg::Vec3& a = ring[i];
        const osg::Vec3& b = ring[j];
        // 边 的 y 范围是否跨越 p.y
        if ((a.y() > p.y()) != (b.y() > p.y()))
        {
            // 求该边在高度 p.y 处的 x 交点，交点在右侧则翻转内外
            const double xCross = a.x() + (p.y() - a.y()) * (b.x() - a.x())
                                  / (b.y() - a.y());
            if (xCross > p.x())
                inside = !inside;
        }
    }
    return inside;
}

// ★ 点在区域内：落在【任一多边形的外环内 且 不在该多边形的任何洞内】
bool pointInRegion(const osg::Vec3d& p, const MapRegion& r)
{
    for (const auto& poly : r.polys)
    {
        if (poly.isEmpty())
            continue;
        if (!pointInRing(p, poly.first()))
            continue;                        // 不在这个外环内，看下一个多边形

        bool inHole = false;
        for (int i = 1; i < poly.size(); ++i)    // 逐个检查洞
            if (pointInRing(p, poly.at(i))) { inHole = true; break; }

        if (!inHole)
            return true;                     // 在外环内且不在洞内 → 在区域内
    }
    return false;
}

// ================================================================
// ★ 多边形面积质心（鞋带公式）
// 优化2 用它给区域名称标注定位：比"顶点平均"更靠近视觉中心。
// 顶点平均在凹多边形（长条形/L 形区县）上可能落到边界之外，
// 面积质心始终在多边形内部，标注不会飘到邻区去。
// 退化情形（面积≈0，如共线点）退回顶点平均兜底。
// ================================================================
static osg::Vec3 ringCentroid(const QVector<osg::Vec3>& ring)
{
    const int n = ring.size();
    if (n == 0)
        return osg::Vec3(0.f, 0.f, 0.f);

    double A = 0.0, cx = 0.0, cy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const osg::Vec3& p1 = ring[i];
        const osg::Vec3& p2 = ring[(i + 1) % n];   // 首尾相连
        const double cross = double(p1.x()) * double(p2.y())
                           - double(p2.x()) * double(p1.y());
        A  += cross;
        cx += (double(p1.x()) + double(p2.x())) * cross;
        cy += (double(p1.y()) + double(p2.y())) * cross;
    }
    if (std::abs(A) > 1e-9)   // 面积质心公式：cx = Σ(p1x+p2x)·cross / 3A
    {
        cx /= (3.0 * A);
        cy /= (3.0 * A);
        return osg::Vec3(float(cx), float(cy), 0.f);
    }

    // 兜底：顶点平均
    osg::Vec3 acc(0.f, 0.f, 0.f);
    for (const osg::Vec3& v : ring)
        acc += v;
    acc /= float(n);
    return acc;
}

// ================================================================
// ③ 构建场景：数据模型 → osg::Group + 区域列表
// ================================================================
MapBuildResult buildSceneEx(const QVector<GeoFeature>& features, double targetSpan)
{
    MapBuildResult out;
    osg::ref_ptr<osg::Group> root = new osg::Group;
    out.root = root;

    // ---- 第 1 遍扫描：经纬度包围盒 → 投影中心 ----
    double minLon = 1e9, maxLon = -1e9, minLat = 1e9, maxLat = -1e9;
    forEachCoord(features, [&](const GeoCoord& c) {
        minLon = qMin(minLon, c.lon);  maxLon = qMax(maxLon, c.lon);
        minLat = qMin(minLat, c.lat);  maxLat = qMax(maxLat, c.lat);
    });
    if (minLon > maxLon)
        return out; // 没有任何坐标，返回空场景

    const GeoCoord center{ (minLon + maxLon) / 2.0, (minLat + maxLat) / 2.0 };

    // ---- 第 2 遍扫描：米制跨度 → 全局缩放 ----
    double maxMeters = 0.0;
    forEachCoord(features, [&](const GeoCoord& c) {
        const MeterXY m = projectMeters(c, center);
        maxMeters = qMax(maxMeters, qMax(qAbs(m.x), qAbs(m.y)));
    });

    // 缩放因子：targetSpan > 0 时按"地图总跨度 = targetSpan"缩放，
    //   让地图与飞机场景（米制）同一尺度；否则维持老的 200 单位模式
    const double scale = (targetSpan > 0.0 && maxMeters > 0.0)
                             ? targetSpan / (2.0 * maxMeters)
                             : ((maxMeters > 0.0) ? 200.0 / maxMeters : 1.0);

    auto toLocal = [center, scale](const GeoCoord& c) {
        const MeterXY m = projectMeters(c, center);
        return osg::Vec3(float(m.x * scale), float(m.y * scale), 0.0f);
    };

    // ---- 分图层装配（每类内容一个 Geode，各管各的渲染状态）----
    osg::ref_ptr<osg::Geode> fillGeode    = new osg::Geode;
    osg::ref_ptr<osg::Geode> outlineGeode = new osg::Geode;
    osg::ref_ptr<osg::Geode> lineGeode    = new osg::Geode;
    osg::ref_ptr<osg::Geode> pointGeode   = new osg::Geode;
    osg::ref_ptr<osg::Geode> labelGeode   = new osg::Geode;

    fillGeode->getOrCreateStateSet()
        ->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
    {
        osg::StateSet* ss = outlineGeode->getOrCreateStateSet();
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        ss->setAttributeAndModes(new osg::LineWidth(1.5f));
    }
    {
        osg::StateSet* ss = lineGeode->getOrCreateStateSet();
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        ss->setAttributeAndModes(new osg::LineWidth(2.0f));
    }
    {
        osg::StateSet* ss = pointGeode->getOrCreateStateSet();
        ss->setMode(GL_LIGHTING, osg::StateAttribute::OFF);
        ss->setAttributeAndModes(new osg::Point(6.0f));
    }
    labelGeode->getOrCreateStateSet()
        ->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

    static const osg::Vec4 kPalette[] = { // 柔和行政区划配色
        osg::Vec4(0.74f, 0.85f, 0.92f, 1.f),
        osg::Vec4(0.79f, 0.90f, 0.81f, 1.f),
        osg::Vec4(0.93f, 0.88f, 0.78f, 1.f),
        osg::Vec4(0.86f, 0.82f, 0.90f, 1.f),
        osg::Vec4(0.83f, 0.89f, 0.89f, 1.f),
        osg::Vec4(0.91f, 0.84f, 0.84f, 1.f),
    };
    const int paletteN = int(sizeof(kPalette) / sizeof(kPalette[0]));

    const osg::Vec4 kOutlineColor(0.16f, 0.21f, 0.30f, 1.0f);
    const osg::Vec4 kLineColor  (0.20f, 0.45f, 0.70f, 1.0f);
    const osg::Vec4 kPointColor (0.85f, 0.30f, 0.30f, 1.0f);

    const QString fontPath = findChineseFont();

    int featureIdx = 0;
    for (const GeoFeature& f : features)
    {
        const osg::Vec4 fillColor = kPalette[featureIdx++ % paletteN];

        // 为本要素登记一个"区域"（一个 Feature = 一个行政区 = 一个告警区域）
        MapRegion region;
        region.name      = f.name;
        region.baseColor = fillColor;

        // ============ 面要素：填充 + 描边 ============
        for (const GeoPolygonData& poly : f.polygons)
        {
            // 登记该多边形的全部环（投影后的场景坐标）：
            //   polyRings[0] = 外环，polyRings[1..] = 洞，供 pointInRegion 使用
            QVector<QVector<osg::Vec3>> polyRings;
            polyRings.reserve(poly.rings.size());
            for (const GeoRing& ring : poly.rings)
            {
                QVector<osg::Vec3> r;
                r.reserve(ring.coords.size());
                for (const GeoCoord& c : ring.coords)
                    r.push_back(toLocal(c));
                polyRings.push_back(r);
            }
            region.polys.push_back(polyRings);

            // ---- 填充面（需 Tessellator 三角化）----
            {
                osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
                osg::ref_ptr<osg::Geometry>  geom  = new osg::Geometry;

                // 每个环一个 POLYGON 图元集；TESS_TYPE_GEOMETRY 会把同一
                // Geometry 里的所有环当作一组轮廓统一三角化，
                // 配合 TESS_WINDING_ODD（奇偶规则）即可正确挖洞
                for (int ri = 0; ri < poly.rings.size(); ++ri)
                {
                    const GeoRing& ring = poly.rings.at(ri);
                    const int start = int(verts->size());
                    if (ri == 0)
                    {
                        // 外环：保持原顺序
                        for (const GeoCoord& c : ring.coords)
                            verts->push_back(toLocal(c));
                    }
                    else
                    {
                        // 洞：反转为顺时针
                        for (int i = ring.coords.size() - 1; i >= 0; --i)
                            verts->push_back(toLocal(ring.coords.at(i)));
                    }
                    const int count = int(verts->size()) - start;
                    if (count >= 3)
                        geom->addPrimitiveSet(new osg::DrawArrays(
                            osg::PrimitiveSet::POLYGON, start, count));
                }
                if (geom->getNumPrimitiveSets() == 0)
                    continue;

                geom->setVertexArray(verts.get());
                osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
                colors->push_back(fillColor);
                geom->setColorArray(colors.get(), osg::Array::BIND_OVERALL);
                geom->setUseVertexBufferObjects(true);

                // 填充几何体要支持运行时改色（入区显亮）：
                //   必须关显示列表缓存 + 声明动态数据方差，
                //   否则 CPU 侧改了颜色数组，GPU 看不到
                geom->setDataVariance(osg::Object::DYNAMIC);
                geom->setUseDisplayList(false);

                osgUtil::Tessellator tess;
                tess.setTessellationType(osgUtil::Tessellator::TESS_TYPE_GEOMETRY);
                tess.setWindingType(osgUtil::Tessellator::TESS_WINDING_ODD);
                tess.setBoundaryOnly(false);
                tess.retessellatePolygons(*geom);

                fillGeode->addDrawable(geom.get());
                region.fillGeoms.push_back(geom.get()); // 登记进区域，显亮时改色用
            }

            // ---- 描边（Z 抬升 0.1，避免与填充面深度冲突）----
            {
                osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
                osg::ref_ptr<osg::Geometry>  geom  = new osg::Geometry;

                for (const GeoRing& ring : poly.rings)
                {
                    const int start = int(verts->size());
                    for (const GeoCoord& c : ring.coords)
                    {
                        osg::Vec3 v = toLocal(c);
                        v.z() = 0.1f;
                        verts->push_back(v);
                    }
                    const int count = int(verts->size()) - start;
                    if (count >= 2)
                        geom->addPrimitiveSet(new osg::DrawArrays(
                            osg::PrimitiveSet::LINE_LOOP, start, count));
                }
                if (geom->getNumPrimitiveSets())
                {
                    geom->setVertexArray(verts.get());
                    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
                    colors->push_back(kOutlineColor);
                    geom->setColorArray(colors.get(), osg::Array::BIND_OVERALL);
                    geom->setUseVertexBufferObjects(true);
                    outlineGeode->addDrawable(geom.get());
                }
            }
        }

        // ============ ★ 优化2：区域名称标注 ============
        // a) 位置改用【面积质心】（第一个多边形的外环），保证在区内；
        // b) 字号随地图跨度缩放 —— 老固定值 6 在 16km 地图上只有芝麻大，
        //    按 targetSpan 的 1.8% 取值：16000 → 约 290，清晰可读；
        //    字号分辨率同步提高到 128，避免大字号发虚。
        if (!region.polys.isEmpty() && !region.polys.first().isEmpty()
            && !f.name.isEmpty())
        {
            osg::Vec3 pos = ringCentroid(region.polys.first().first());
            pos.z() = 1.0f;   // 抬到填充面之上，避免深度冲突

            const float charSize = (targetSpan > 0.0)
                                       ? float(targetSpan * 0.018) : 6.0f;

            osg::ref_ptr<osgText::Text> label = new osgText::Text;
            if (!fontPath.isEmpty())
                label->setFont(fontPath.toStdString());
            // 中文字符串必须显式声明 UTF-8 编码，否则会按 Latin1 解析成乱码
            label->setText(osgText::String(f.name.toStdString(),
                                           osgText::String::ENCODING_UTF8));
            label->setPosition(pos);
            label->setCharacterSize(charSize);
            label->setFontResolution(128, 128);   // ★ 大字号需要更高纹理分辨率
            label->setAlignment(osgText::TextBase::CENTER_CENTER);
            label->setAutoRotateToScreen(true);   // 始终面向相机，倾斜视角也可读
            label->setColor(osg::Vec4(0.13f, 0.16f, 0.22f, 1.f));
            label->setBackdropType(osgText::Text::OUTLINE);
            label->setBackdropColor(osg::Vec4(1.f, 1.f, 1.f, 0.85f));
            labelGeode->addDrawable(label.get());
        }

        // ============ 线要素 ============
        if (!f.lines.isEmpty())
        {
            osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
            osg::ref_ptr<osg::Geometry>  geom  = new osg::Geometry;

            for (const QVector<GeoCoord>& line : f.lines)
            {
                const int start = int(verts->size());
                for (const GeoCoord& c : line)
                    verts->push_back(toLocal(c));
                const int count = int(verts->size()) - start;
                if (count >= 2)
                    geom->addPrimitiveSet(new osg::DrawArrays(
                        osg::PrimitiveSet::LINE_STRIP, start, count));
            }
            if (geom->getNumPrimitiveSets())
            {
                geom->setVertexArray(verts.get());
                osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
                colors->push_back(kLineColor);
                geom->setColorArray(colors.get(), osg::Array::BIND_OVERALL);
                geom->setUseVertexBufferObjects(true);
                lineGeode->addDrawable(geom.get());
            }
        }

        // ============ 点要素 ============
        if (!f.points.isEmpty())
        {
            osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
            for (const GeoCoord& c : f.points)
                verts->push_back(toLocal(c));

            osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;
            geom->setVertexArray(verts.get());
            geom->addPrimitiveSet(new osg::DrawArrays(
                osg::PrimitiveSet::POINTS, 0, int(verts->size())));
            osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
            colors->push_back(kPointColor);
            geom->setColorArray(colors.get(), osg::Array::BIND_OVERALL);
            geom->setUseVertexBufferObjects(true);
            pointGeode->addDrawable(geom.get());
        }

        // 只有多边形数据的 Feature 才算"区域"（纯点/线的没有入区概念）
        if (!region.polys.isEmpty())
            out.regions.push_back(region);
    }

    root->addChild(fillGeode.get());
    root->addChild(outlineGeode.get());
    root->addChild(lineGeode.get());
    root->addChild(pointGeode.get());
    root->addChild(labelGeode.get());
    return out;
}

osg::ref_ptr<osg::Node> buildScene(const QVector<GeoFeature>& features)
{
    return buildSceneEx(features, 0.0).root;
}

} // namespace geojson
