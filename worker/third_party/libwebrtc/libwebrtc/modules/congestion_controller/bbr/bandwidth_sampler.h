#ifndef MODULES_CONGESTION_CONTROLLER_BBR_BANDWIDTH_SAMPLER_H_
#define MODULES_CONGESTION_CONTROLLER_BBR_BANDWIDTH_SAMPLER_H_

#include "absl/types/optional.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/bbr/packet_number_indexed_queue.h"

namespace webrtc {
namespace bbr {

// 测试类声明，允许测试代码访问BandwidthSampler的私有成员。
namespace test {
class BandwidthSamplerPeer;
}

// BandwidthSample结构体定义，用于存储带宽采样的结果。
struct BandwidthSample {
  // 该特定样本的带宽。如果没有有效的带宽样本可用，则为零。
  DataRate bandwidth;

  // 该特定样本的RTT测量值。如果没有RTT样本可用，则为零。不纠正延迟的ack时间。
  TimeDelta rtt;

  // 标示样本可能因为发送方没有足够的数据来饱和链路而人为偏低。
  bool is_app_limited;

  BandwidthSample()
      : bandwidth(DataRate::Zero()),
        rtt(TimeDelta::Zero()),
        is_app_limited(false) {}
};

// BandwidthSampler跟踪发送和确认的数据包，并为每个确认的数据包输出一个带宽样本。
// 样本是针对单个数据包取的，并没有过滤；消费者需要自己过滤带宽样本。在某些情况下，
// 采样器会严重低估带宽，因此推荐使用至少一个RTT大小的最大值过滤器。
class BandwidthSampler {
 public:
  BandwidthSampler();
  ~BandwidthSampler();

  // 将发送的数据包信息输入到采样器中。假设所有数据包按顺序发送。
  // 直到数据包被确认或声明丢失之前，关于该数据包的信息不会从采样器中释放。
  void OnPacketSent(Timestamp sent_time,
                    int64_t packet_number,
                    DataSize data_size,
                    DataSize data_in_flight);

  // 通知采样器|packet_number|被确认。返回一个带宽样本。
  // 如果没有可用的带宽样本，带宽设置为DataRate::Zero()。
  BandwidthSample OnPacketAcknowledged(Timestamp ack_time,
                                       int64_t packet_number);

  // 通知采样器一个数据包被认为丢失，不再跟踪它。
  void OnPacketLost(int64_t packet_number);

  // 通知采样器连接当前是应用受限的，导致采样器进入应用受限阶段。
  // 阶段会自行过期。
  void OnAppLimited();

  // 移除所有小于指定数据包编号的数据包。
  void RemoveObsoletePackets(int64_t least_unacked);

  // 返回接收方当前确认的数据总量。
  DataSize total_data_acked() const;

  // 导出的应用受限信息，用于调试。
  bool is_app_limited() const;
  int64_t end_of_app_limited_phase() const;

 private:
  friend class test::BandwidthSamplerPeer;

  // ConnectionStateOnSentPacket结构体表示发送数据包时的信息以及数据包发送时连接的状态，
  // 特别是在数据包发送时刻最近一次被确认的数据包的信息。
  struct ConnectionStateOnSentPacket {
    Timestamp sent_time; // 数据包发送时间。
    DataSize size; // 数据包大小。
    DataSize total_data_sent; // 发送该数据包时的总发送数据量，包括数据包本身。
    DataSize total_data_sent_at_last_acked_packet; // 发送该数据包时最后一个确认的数据包时的总发送数据量。
    absl::optional<Timestamp> last_acked_packet_sent_time; // 发送该数据包时最后一个确认的数据包的发送时间。
    absl::optional<Timestamp> last_acked_packet_ack_time; // 发送该数据包时最后一个确认的数据包的确认时间。
    DataSize total_data_acked_at_the_last_acked_packet; // 发送该数据包时最后一个确认的数据包时的总确认数据量。
    bool is_app_limited; // 发送该数据包时是否处于应用受限状态。

    // 快照构造函数。记录带宽采样器的当前状态。
    ConnectionStateOnSentPacket(Timestamp sent_time,
                                DataSize size,
                                const BandwidthSampler& sampler);

    // 默认构造函数。需要将此结构体放入PacketNumberIndexedQueue中。
    ConnectionStateOnSentPacket();
    ~ConnectionStateOnSentPacket();
  };

  // 处理实际带宽计算的内部方法，而外部方法处理检索和移除|sent_packet|。
  BandwidthSample OnPacketAcknowledgedInner(
      Timestamp ack_time,
      int64_t packet_number,
      const ConnectionStateOnSentPacket& sent_packet);

private:
  DataSize total_data_sent_; // 连接期间受拥塞控制的总发送数据量。
  DataSize total_data_acked_; // 已经被确认的受拥塞控制的总数据量。
  DataSize total_data_sent_at_last_acked_packet_; // 最后一个被确认的数据包发送时的总发送数据量。
  absl::optional<Timestamp> last_acked_packet_sent_time_; // 最后一个被确认的数据包的发送时间。
  absl::optional<Timestamp> last_acked_packet_ack_time_; // 最近被确认的数据包的确认时间。
  int64_t last_sent_packet_; // 最近发送的数据包。
  bool is_app_limited_; // 标记带宽采样器当前是否处于应用受限阶段。
  int64_t end_of_app_limited_phase_; // 将导致采样器退出应用受限阶段的下一个被确认的数据包。
  PacketNumberIndexedQueue<ConnectionStateOnSentPacket> connection_state_map_; // 发送时点的连接状态记录，按数据包编号索引。
  BandwidthSample bandwidth_sample_; // 当前的带宽样本。
};

}  // namespace bbr
}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_BBR_BANDWIDTH_SAMPLER_H_
