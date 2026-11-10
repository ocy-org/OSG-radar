// ============================================================
// 数据层（Model）
// 本文件只做"数学与数据"，禁止 include 任何 osg 渲染类
//    （Geometry/Geode/Camera...）；只允许用数学类型 osg::Vec3d
//    （纯数值运算，不需要 GL 上下文）。
// ============================================================
#ifndef SITUATIONCORE_H
#define SITUATIONCORE_H

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <osg/Math>     // osg::PI / PI_2
#include <osg/Vec3d>    // 仅当数学类型用

namespace situ {

// ---------------- 五角星航线：真值的唯一来源 ----------------
// 一个数据两个消费者 → 必须下沉到数据层，保证雷达测的和显示画的是同一架飞机。
constexpr double kStarR   = 5000.0;   // 外接圆半径（米）
constexpr double kStarH   = 800.0;    // 飞行高度（米）
constexpr double kSegTime = 6.0;      // 每段飞行时长（秒）

// 第 i 个外顶点（i=0..4，第 0 个在正北）
inline osg::Vec3d starVertex(int i)
{
    const double a = osg::PI_2 + i * (2.0 * osg::PI / 5.0);
    return osg::Vec3d(kStarR * std::cos(a), kStarR * std::sin(a), kStarH);
}

// 星形飞行顺序（0→2→4→1→3 连线才是五角星），尾接回首
inline int starSeq(int k)
{
    static const int s[6] = { 0, 2, 4, 1, 3, 0 };
    return s[k];
}

// 时刻 t 的真值位置（分段线性插值 = 过程1 updateTruth 的数学内核）
inline osg::Vec3d truthPos(double t)
{
    const double tau = std::fmod(t, kSegTime * 5.0);   // 圈内时间
    const int    seg = int(tau / kSegTime);            // 当前段 0..4
    const double u   = (tau - seg * kSegTime) / kSegTime;
    const osg::Vec3d a = starVertex(starSeq(seg));
    const osg::Vec3d b = starVertex(starSeq(seg + 1));
    return a + (b - a) * u;
}

// 时刻 t 的真值运动方向（单位向量，用于对准机头）
inline osg::Vec3d truthVel(double t)
{
    const double tau = std::fmod(t, kSegTime * 5.0);
    const int    seg = int(tau / kSegTime);
    osg::Vec3d d = starVertex(starSeq(seg + 1)) - starVertex(starSeq(seg));
    d.normalize();
    return d;
}

// ---------------- 高斯噪声：Box-Muller 变换 ----------------
// 两个独立均匀分布 U1,U2 ∈ (0,1) → 标准正态 N(0,1)：
//   z = sqrt(-2·ln U1)·cos(2π·U2)
// 分母分子各 +1/+2 是为了避免 log(0) 奇点。
inline double gaussRand()
{
    const double u1 = (std::rand() + 1.0) / (RAND_MAX + 2.0);
    const double u2 = (std::rand() + 1.0) / (RAND_MAX + 2.0);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * osg::PI * u2);
}

// ---------------- 点迹 ----------------
// 点迹 = 雷达的一次"瞬时测量"。注意雷达报告的是【极坐标】
// （距离+方位）——这是传感器的天然形态，xy 换算由显示侧做。
struct Plot {
    double t;        // 测量时刻
    double range;    // 测量距离（米）
    double azimuth;  // 测量方位（弧度）
};

// 极坐标 → 直角坐标。z 恒为 0：二坐标雷达只能测 R/θ，测不出高度
inline osg::Vec3d polarToWorld(const Plot& p)
{
    return osg::Vec3d(p.range * std::cos(p.azimuth),
                      p.range * std::sin(p.azimuth), 0.0);
}

// ---------------- 雷达模拟器 ----------------
class RadarSim {
public:
    RadarSim()
    {
        // 固定种子：每次运行噪声序列相同，方便对比实验。
        // 想每次不同改成：std::srand(unsigned(std::time(nullptr)));
        std::srand(20240001);
    }

    // 测量误差：距离标准差 80m，方位标准差 0.004rad（约 0.23°）
    void setNoise(double sigmaR, double sigmaA)
    { m_sigmaR = sigmaR; m_sigmaA = sigmaA; }

    // 扫描周期：每 0.5 秒出一个点迹（模拟天线转一圈测一次）
    void setScanPeriod(double sec) { m_scanT = sec; }

    // 推进到时刻 t。若本拍该出一个点迹则填入 out 并返回 true。
    // 这是数据层对外的唯一入口——将来接真实雷达，只需替换本函数内部。
    bool step(double t, Plot& out)
    {
        if (t - m_last < m_scanT) return false;    // 天线还没转完一圈
        m_last = t;

        const osg::Vec3d p = truthPos(t);          // ① 取真值位置
        const double trueR = std::hypot(p.x(), p.y());   // ② 真值距离
        const double trueA = std::atan2(p.y(), p.x());   //    真值方位

        out.t       = t;
        out.range   = trueR + gaussRand() * m_sigmaR;  // ③ 叠加测量误差
        out.azimuth = trueA  + gaussRand() * m_sigmaA;
        return true;
    }

private:
    double m_scanT  = 0.5;
    double m_sigmaR = 80.0;
    double m_sigmaA = 0.004;
    double m_last   = -1e9;    // 保证第一次调用立即出点
};

// ---------------- 航迹 ----------------
// 航迹 = 滤波器对目标状态的持续估计。对比 Plot（一次瞬时带噪观测），
// Track 是"这个目标现在在哪、往哪去"的稳定答案。
struct Track {
    bool       valid = false;   // 是否已起始（收到第一个点迹后为 true）
    osg::Vec3d pos;             // 滤波位置（z恒0：雷达测不到高度）
    osg::Vec3d vel;             // 滤波速度（m/s）
    double     t     = 0.0;     // 本状态对应的时刻
    int        hits  = 0;       // 累计关联成功的点迹数 ≈ 航迹质量
};

// ---------------- α-β 航迹处理器 ----------------
// 每来一个点迹执行一轮"预测→关联→修正"。α-β 滤波是卡尔曼滤波
// 在匀速模型下的常增益简化版：增益固定、计算量极小。
class TrackProcessor {
public:
    void setGain(double alpha, double beta) { m_alpha = alpha; m_beta = beta; }
    void setGate(double g)                  { m_gate = g; }
    void setMaxMissed(int n)                { m_maxMissed = n; }  // ★新增
    int  rejected() const                   { return m_rej; }
    int  missStreak() const                 { return m_miss; }    // ★连续失联计数

    void feed(const Plot& p)
    {
        const osg::Vec3d m = polarToWorld(p);

        // 航迹起始（或失联重启后的重新起始）
        if (!m_trk.valid) { init(m, p.t); return; }

        const double dt = p.t - m_trk.t;
        if (dt <= 0) return;

        // ① 预测：匀速模型外推
        const osg::Vec3d pred = m_trk.pos + m_trk.vel * dt;
        const osg::Vec3d res  = m - pred;

        // ② 关联波门
        if (res.length() > m_gate) {
            ++m_rej;  ++m_miss;
            // ★★ 关键修复：连续 maxMissed 个点迹都进不了波门，
            //     判定原航迹已失效（目标急机动，匀速外推彻底跑偏），
            //     用当前点迹重新起始航迹。
            //     真实系统对应：航迹终止 + 未关联点迹起始新航迹。
            //     没有这条，波门一旦拒收就死锁——预测永不修正，
            //     残差每拍扩大 v·Δt·2sin(θ/2)≈1500m，再也进不了门。
            if (m_miss >= m_maxMissed) init(m, p.t);
            return;
        }
        m_miss = 0;                        // 关联成功，清零连续失联

        // ③ 修正
//        m_trk.pos  = pred + res * m_alpha;
//        m_trk.vel += res * (m_beta / dt);
//        m_trk.t    = p.t;
//        m_trk.hits++;
        // ③ 修正（机动自适应增益版）
        // 残差远超测量噪声(σ≈100~200m) ⇒ 目标在机动。固定小增益下矢量
        // 要摆 4~6 拍才跟上 144° 急转；检测到机动就把这一拍增益临时加大，
        // 1~2 拍完成转向（代价：这几拍噪声稍大）。600 卡在噪声与机动残差之间。
        const double resLen = res.length();
        const double a = resLen > 600.0 ? 0.90 : m_alpha;
        const double b = resLen > 600.0 ? 0.90 : m_beta;
        m_trk.pos  = pred + res * a;
        m_trk.vel += res * (b / dt);
    }

    // 漏测时的"滑行"保持（本场景不触发，但真实系统标配）
    void missed(double t)
    {
        if (!m_trk.valid) return;
        const double dt = t - m_trk.t;
        m_trk.pos += m_trk.vel * dt;
        m_trk.t    = t;
    }

    const Track& track() const { return m_trk; }

private:
    void init(const osg::Vec3d& m, double t)      // ★ 起始逻辑提取成函数
    {
        m_trk.valid = true;
        m_trk.pos.set(m.x(), m.y(), 0.0);
        m_trk.vel.set(0, 0, 0);
        m_trk.t    = t;
        m_trk.hits = 1;
        m_miss     = 0;
    }

    Track  m_trk;
    double m_alpha = 0.65;
    double m_beta  = 0.31;
    double m_gate  = 3000.0;   // ★ 1500→3000：容纳急转弯后第一拍的~1500m残差
    int    m_rej       = 0;
    int    m_miss      = 0;    // ★ 连续被拒计数
    int    m_maxMissed = 3;    // ★ 连续3拍(1.5s)进不了门 → 重新起始
};

// ============================================================
// 以下为过程4新增：区域包含判断（纯空间数学，渲染无关）
// 注意：全部在 xy 平面判断 —— 雷达世界是二维的，高度 z 不参与。
// ============================================================

// 目标是否在扇形区域内（az0/az1 为弧度，数学约定 0°=东逆时针）
inline bool insideSector(const osg::Vec3d& p, double az0, double az1, double maxR)
{
    if (std::hypot(p.x(), p.y()) > maxR) return false;   // 距离条件
    double az = std::atan2(p.y(), p.x());                // (-π, π]
    if (az < 0) az += 2.0 * osg::PI;                     // 归一化到 [0, 2π)
    return az >= az0 && az <= az1;
}

// 目标是否在圆形区域内（圆心 cx,cy；比较用 xy 平面距离）
// ★ 若误用三维距离：飞机 z=800 会被算进去，导致边界附近误判
inline bool insideCircleXY(const osg::Vec3d& p, double cx, double cy, double radius)
{
    const double dx = p.x() - cx, dy = p.y() - cy;
    return dx * dx + dy * dy <= radius * radius;
}

} // namespace situ
#endif
