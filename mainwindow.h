// ============================================================
// MainWindow —— 主窗口（过程1 版）
// 职责：① 加载/准备飞机模型  ② 组装界面  ③ 用 QTimer 维护仿真时钟，
//       每 33ms 推进一次并驱动 SituationScene。
// ============================================================
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <osg/Group>
#include <QEvent>
#include "OSGWidget.h"
#include "SituationCore.h"
#define GEOFILEPATH "D:/software/OpenSceneG/geoddata/china.geo.Json"

class SituationScene;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QString& modelFile = QString(),
                        QWidget* parent = nullptr);
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;   // ★ 事件过滤器

private slots:
    void onTick();          // 仿真时钟节拍：推进时间 → 驱动场景
    void onPause(bool on);  // 暂停/继续

private:
    OSGWidget*       m_view   = nullptr;   // 3D 视图
    osg::ref_ptr<osg::Group> m_root;       // 场景根
    SituationScene*  m_scene  = nullptr;

    situ::RadarSim  m_radar;            // 雷达模拟器
    situ::TrackProcessor   m_tracker;   // ★ 数据层：航迹处理器

    QTimer m_timer;
    double m_simTime = 0.0;              // 仿真时钟（秒）—— 与真实墙钟解耦，
    double m_dt      = 0.033;            // 每帧推进 33ms
};
#endif
