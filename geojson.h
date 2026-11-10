// ============================================================
// geojson.h —— GeoJSON 地图加载 + 行政区"区域"数据
//   路径暴露出去，供态势层机载标签显示中文区县名使用
// ============================================================
#ifndef GEOJSON_H
#define GEOJSON_H

#include <QString>
#include <QVector>
#include <osg/ref_ptr>
#include <osg/Geometry>   // MapRegion 中要存 osg::ref_ptr<osg::Geometry>
#include <osg/Node>
#include <osg/Vec3>       // MapRegion 存投影后的 osg::Vec3 环
#include <osg/Vec3d>      // pointInRegion 参数类型
#include <osg/Vec4>

namespace geojson {

// ---------------- GeoJSON 数据模型 ----------------
struct GeoCoord          // 一个经纬度点（GeoJSON 顺序：先经度后纬度）
{
    double lon = 0.0;
    double lat = 0.0;
};

struct GeoRing           // 一条闭合环（外环或洞）
{
    QVector<GeoCoord> coords;
};

struct GeoPolygonData    // 一个多边形 = 外环 + 若干洞
{
    QVector<GeoRing> rings;
};

struct GeoFeature        // 一个 Feature（一个行政区）
{
    QString name;
    QString geomType;
    QVector<GeoPolygonData> polygons;
    QVector<QVector<GeoCoord>> lines;
    QVector<GeoCoord> points;
};

// ============================================================
// 行政区"区域"概念（供态势系统做入区告警 + 区域显亮）
// 一个区域 = 一个 GeoFeature 的全部多边形（MultiPolygon 有多块）
// 坐标全部是【投影 + 缩放后】的场景平面坐标（xy），与飞机同一坐标系
// ============================================================
struct MapRegion
{
    QString name;   // 区域名（properties.name，可含中文）

    // polys[k] = 第 k 个多边形；polys[k][0] = 外环；polys[k][1..] = 洞
    // ★ 必须三层嵌套：MultiPolygon 有多个独立外环，各带各自的洞
    QVector<QVector<QVector<osg::Vec3>>> polys;

    // 该区域对应的填充几何体（MultiPolygon 会有多个），高亮时逐个改色
    QVector<osg::ref_ptr<osg::Geometry>> fillGeoms;

    osg::Vec4 baseColor;   // 常态填充色（退出告警时恢复）
};

struct MapBuildResult
{
    osg::ref_ptr<osg::Node> root;   // 地图整体（直接 addChild 进场景根）
    QVector<MapRegion> regions;     // 全部区域（交给态势逐帧判定）
};

// ---------------- 接口 ----------------

// ① 解析 GeoJSON 文件 → 数据模型
bool loadFile(const QString& path, QVector<GeoFeature>& outFeatures, QString& err);

// ② 老接口（缩放到 200 单位跨度），保留兼容
osg::ref_ptr<osg::Node> buildScene(const QVector<GeoFeature>& features);

// ★ targetSpan = 地图总跨度（场景单位，与飞机同为"米"）
//   传 16000 → 地图占满 16km，与五角星航线（R=5km）同尺度；
//   传 0     → 回退为老 200 单位模式。
MapBuildResult buildSceneEx(const QVector<GeoFeature>& features,
                            double targetSpan = 0.0);

// ★ 点在区域内判断（xy 平面射线法，自动处理洞与 MultiPolygon）
bool pointInRegion(const osg::Vec3d& p, const MapRegion& r);

// ★ 返回本机可用的中文字体路径（找不到则返回空串）。
//   osgText 默认字体没有 CJK 字形，中文会显示成方框；
//   机载标签等其他 osgText 想显示中文，都用它取字体。
QString chineseFont();

} // namespace geojson

#endif // GEOJSON_H
