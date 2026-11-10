// ============================================================
// SituationScene —— 3D 态势图层
// （地图 + 飞机 + 点迹图层 + 航迹图层 + 行政区告警 + HUD）
// 职责：创建本过程全部静态元素；提供 updateTruth(t) 每帧驱动飞机。
// ★ 本次修改：隐藏蓝色五角星航线；机载标签实时显示所在区域名。
// ============================================================
#ifndef SITUATIONSCENE_H
#define SITUATIONSCENE_H

#include <osg/Group>
#include <osg/MatrixTransform>
#include <osgText/Text>
#include <osg/Vec3d>
#include <osg/Geometry>
#include <QVector>
#include <QString>      // ★ 新增：m_curRegion 用
#include "SituationCore.h"
#include "SceneUtil.h"
#include "geojson.h"

class SituationScene
{
public:
    // root : 场景根节点（本类把所有图层挂上去）
    // model : 已处理好的飞机模型（缩放/材质已在 MainWindow 处理）
    explicit SituationScene(osg::Group* root, osg::Node* model);

    // 核心：动画 = 时间 → (位置, 姿态) 的映射函数。
    // MainWindow 每帧用仿真时钟调用它，飞机就动起来。
    void updateTruth(double t);                 // 真值飞机（每帧）

    void addPlot(const situ::Plot& plot);       // 点迹（有点迹时）
    void updateTrack(const situ::Track& trk);   // 航迹（有点迹时）

    void setHudSize(int w, int h);              // 窗口尺寸变化时由 MainWindow 转发

    // 挂载 GeoJSON 行政区地图（替代原网格 + 固定标绘区）。
    //   map.root 挂进场景；map.regions 存下用于逐帧入区判定。
    void attachMap(const geojson::MapBuildResult& map);
    // 地图加载失败时的兜底——还原原 16km 网格地面
    void attachGrid();

private:
    void buildStarRoute();  // ★ 保留函数但不再调用（优化1：隐藏蓝色航线）
    void buildAircraft();   // 飞机 + 标签 + 垂直线
    void buildPlots();      // 点迹图层
    void buildTracks();     // 航迹图层
    void buildHud();        // HUD 告警面板

    // 行政区入区判定 + 边沿告警 + 区域高亮 + 机载标签刷新
    void updateMapZones(const osg::Vec3d& p, double t);

    osg::ref_ptr<osg::Group>            m_root;     // 场景根
    osg::ref_ptr<osg::MatrixTransform>  m_airMt;    // 飞机变换节点（动画作用对象）
    osg::ref_ptr<osgText::Text>         m_airLabel; // 机载标签（★ 现在显示区名）

    // 点迹图层
    osg::ref_ptr<osg::Geometry> m_plotGeom;     // 点迹动态几何体
    unsigned m_plotCap = 240;                   // 内存上限：240点≈2分钟历史

    // 航迹图层
    osg::ref_ptr<osg::Geometry> m_trkLine;      // 航迹历史线【增长式】
    osg::ref_ptr<osg::Geometry> m_velLine;      // 速度矢量【定长改写式】
    osg::ref_ptr<osgText::Text> m_trkLabel;     // 数据块 = 航迹号 + 速度
    unsigned m_trkCap = 240;

    // 行政区告警状态
    QVector<geojson::MapRegion> m_regions;  // 区域列表（attachMap 时填入）
    QVector<bool> m_regionIn;               // 与 m_regions 一一对应的
                                            // "上一帧是否在区内"（边沿检测用）

    QString m_curRegion;   // ★ 新增：机载标签当前显示的区域名（缓存）。
                           // 与本帧判定结果比较，变化才 setText ——
                           // 避免每帧重建文字纹理，也避免 30Hz 刷屏

    // HUD 告警面板
    su::HudAlertPanel m_hud;
};

#endif
