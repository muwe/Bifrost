// 定义在WebRTC项目中，基于Chromium的Quic实现的带宽采样器。
#include "modules/congestion_controller/bbr/bandwidth_sampler.h"

#include <algorithm>
#include "rtc_base/logging.h"

namespace webrtc {
namespace bbr {

// 定义最大跟踪的数据包数量，防止内存占用过高。
namespace {
constexpr int64_t kMaxTrackedPackets = 10000;
}

// BandwidthSampler构造函数，初始化所有成员变量。
BandwidthSampler::BandwidthSampler()
    : total_data_sent_(DataSize::Zero()), // 已发送数据总量初始化为0
      total_data_acked_(DataSize::Zero()), // 已确认数据总量初始化为0
      total_data_sent_at_last_acked_packet_(DataSize::Zero()), // 上一个确认的数据包时已发送数据总量初始化为0
      last_acked_packet_sent_time_(), // 上一个确认的数据包的发送时间
      last_acked_packet_ack_time_(), // 上一个确认的数据包的确认时间
      last_sent_packet_(0), // 最后一个发送的数据包编号
      is_app_limited_(false), // 应用限制状态初始化为false
      end_of_app_limited_phase_(0), // 应用限制阶段的结束初始化为0
      connection_state_map_() { // 连接状态映射初始化为空

      RTC_LOG(LS_INFO) << "RTC_LOG::BandwidthSampler.";
} 

BandwidthSampler::~BandwidthSampler() {
      RTC_LOG(LS_INFO) << "~RTC_LOG::BandwidthSampler.";

}

// 当一个数据包被发送时调用，记录数据包信息并更新发送统计。
void BandwidthSampler::OnPacketSent(Timestamp sent_time,
                                    int64_t packet_number,
                                    DataSize data_size,
                                    DataSize data_in_flight) {
  last_sent_packet_ = packet_number; // 更新最后一个发送的数据包编号

  total_data_sent_ += data_size; // 更新已发送数据总量

  // 如果当前没有其他数据包在传输中，认为这是一个新的传输开端，可以作为带宽采样的一个起点。
  if (data_in_flight.IsZero()) {
    last_acked_packet_ack_time_ = sent_time; // 设置最后一个确认包的确认时间为当前发送时间
    total_data_sent_at_last_acked_packet_ = total_data_sent_; // 更新在最后一个确认的数据包时已发送的数据总量
    last_acked_packet_sent_time_ = sent_time; // 设置最后一个确认包的发送时间为当前发送时间
  }

  // 如果跟踪的数据包数量超过最大值，记录警告日志。
  if (!connection_state_map_.IsEmpty() &&
      packet_number > connection_state_map_.last_packet() + kMaxTrackedPackets) {
    RTC_LOG(LS_WARNING)
        << "BandwidthSampler in-flight packet map has exceeded maximum "
           "number of tracked packets.";
  }

  // 尝试将数据包信息插入到连接状态映射中，如果失败则记录警告日志。
  bool success = connection_state_map_.Emplace(packet_number, sent_time, data_size, *this);
  // RTC_LOG(LS_INFO) << "BandwidthSampler OnPacketSent " 
  //                     << " ,success:" << rtc::ToString(success)
  //                     << " ,bandwidth:" << rtc::ToString(bandwidth_sample_.bandwidth.bps()) << " bps"
  //                     << " ,rtt:" << rtc::ToString(bandwidth_sample_.rtt.ms()) << " ms"
  //                     << " ,is_app_limited:" << rtc::ToString(bandwidth_sample_.is_app_limited)
  //                     << " ,total_data_sent_last ack:" << rtc::ToString(total_data_sent_at_last_acked_packet_.bytes()) << " bytes"
  //                     << " ,total_data_sent:" << rtc::ToString(total_data_sent_.bytes()) << " bytes"
  //                     << " ,total_data_acked:" << rtc::ToString(total_data_acked_.bytes()) << "bytes"
  //                     << " ,packet_number" << rtc::ToString(packet_number)
  //                     << " ,packet numbers " << rtc::ToString(connection_state_map_.number_of_present_entries())
  //                     ;
  ;

  if (!success)
    RTC_LOG(LS_WARNING) << "BandwidthSampler failed to insert the packet "
                           "into the map, most likely because it's already "
                           "in it.";
}

// 当一个数据包被确认时调用，计算并返回该数据包的带宽样本。
BandwidthSample BandwidthSampler::OnPacketAcknowledged(Timestamp ack_time,
                                                       int64_t packet_number) {
  ConnectionStateOnSentPacket* sent_packet_pointer = connection_state_map_.GetEntry(packet_number);
  if (sent_packet_pointer == nullptr) {
    return BandwidthSample(); // 如果找不到数据包信息，返回空的带宽样本
  }
  BandwidthSample sample = OnPacketAcknowledgedInner(ack_time, packet_number, *sent_packet_pointer);
  bandwidth_sample_ = sample;
  connection_state_map_.Remove(packet_number); // 从映射中移除已确认的数据包

  // RTC_LOG(LS_INFO) << "BandwidthSampler OnPacketAcknowledged " 
  //                     << " ,bandwidth:" << rtc::ToString(bandwidth_sample_.bandwidth.bps()) << " bps"
  //                     << " ,rtt:" << rtc::ToString(bandwidth_sample_.rtt.ms()) << " ms"
  //                     << " ,is_app_limited:" << rtc::ToString(bandwidth_sample_.is_app_limited)
  //                     << " ,total_data_sent_last ack:" << rtc::ToString(total_data_sent_at_last_acked_packet_.bytes()) << " bytes"
  //                     << " ,total_data_sent:" << rtc::ToString(total_data_sent_.bytes()) << " bytes"
  //                     << " ,total_data_acked:" << rtc::ToString(total_data_acked_.bytes()) << "bytes"
  //                     << " ,packet_number " << rtc::ToString(packet_number)
  //                     << " ,packet numbers " << rtc::ToString(connection_state_map_.number_of_present_entries())
  //                     ;

  return sample; // 返回计算得到的带宽样本
}

// 实际进行带宽样本计算的内部方法。
BandwidthSample BandwidthSampler::OnPacketAcknowledgedInner(
    Timestamp ack_time,
    int64_t packet_number,
    const ConnectionStateOnSentPacket& sent_packet) {
  // 更新统计信息
  total_data_acked_ += sent_packet.size;
  total_data_sent_at_last_acked_packet_ = sent_packet.total_data_sent;
  last_acked_packet_sent_time_ = sent_packet.sent_time;
  last_acked_packet_ack_time_ = ack_time;

  // 如果当前数据包发送时连接不是应用限制的，并且该数据包编号超过了应用限制阶段的结束，标记应用限制结束。
  if (is_app_limited_ && packet_number > end_of_app_limited_phase_) {
    is_app_limited_ = false;
  }

  // 如果发送当前数据包时没有已确认的数据包，意味着无法生成有效的带宽样本。
  if (!sent_packet.last_acked_packet_sent_time || !sent_packet.last_acked_packet_ack_time) {
    return BandwidthSample();
  }

  // 计算发送速率和确认速率，取二者的最小值作为带宽样本。
  DataRate send_rate = DataRate::Infinity(); // 初始化发送速率为无穷大
  if (sent_packet.sent_time > *sent_packet.last_acked_packet_sent_time) {
    DataSize sent_delta = sent_packet.total_data_sent - sent_packet.total_data_sent_at_last_acked_packet;
    TimeDelta time_delta = sent_packet.sent_time - *sent_packet.last_acked_packet_sent_time;
    send_rate = sent_delta / time_delta; // 计算发送速率
  }

  // 确保当前数据包的确认时间大于上一个数据包的确认时间，以避免除零错误。
  if (ack_time <= *sent_packet.last_acked_packet_ack_time) {
    RTC_LOG(LS_WARNING)
        << "Time of the previously acked packet is larger than the time "
           "of the current packet.";
    return BandwidthSample();
  }
  DataSize ack_delta = total_data_acked_ - sent_packet.total_data_acked_at_the_last_acked_packet;
  TimeDelta time_delta = ack_time - *sent_packet.last_acked_packet_ack_time;
  DataRate ack_rate = ack_delta / time_delta; // 计算确认速率

  // 生成带宽样本
  BandwidthSample sample;
  sample.bandwidth = std::min(send_rate, ack_rate); // 带宽样本取发送速率和确认速率的最小值
  sample.rtt = ack_time - sent_packet.sent_time; // 计算往返时间
  sample.is_app_limited = sent_packet.is_app_limited; // 标记样本是否在应用限制期间生成
  return sample; // 返回带宽样本
}

// 当一个数据包被丢失时调用，从连接状态映射中移除数据包信息。
void BandwidthSampler::OnPacketLost(int64_t packet_number) {
  connection_state_map_.Remove(packet_number);
}

// 标记连接进入应用限制状态。
void BandwidthSampler::OnAppLimited() {
  is_app_limited_ = true; // 标记为应用限制
  end_of_app_limited_phase_ = last_sent_packet_; // 更新应用限制阶段的结束为最后一个发送的数据包
}

// 移除过时的数据包信息。
void BandwidthSampler::RemoveObsoletePackets(int64_t least_unacked) {
  // 循环移除所有小于最小未确认数据包编号的数据包信息。
  while (!connection_state_map_.IsEmpty() &&
         connection_state_map_.first_packet() < least_unacked) {
    connection_state_map_.Remove(connection_state_map_.first_packet());
  }
}

// 返回已确认数据总量。
DataSize BandwidthSampler::total_data_acked() const {
  return total_data_acked_;
}

// 返回是否处于应用限制状态。
bool BandwidthSampler::is_app_limited() const {
  return is_app_limited_;
}

// 返回应用限制阶段的结束数据包编号。
int64_t BandwidthSampler::end_of_app_limited_phase() const {
  return end_of_app_limited_phase_;
}

// 构造函数和析构函数定义，用于在发送数据包时记录连接的状态。
BandwidthSampler::ConnectionStateOnSentPacket::ConnectionStateOnSentPacket(
    Timestamp sent_time,
    DataSize size,
    const BandwidthSampler& sampler)
    : sent_time(sent_time), // 数据包发送时间
      size(size), // 数据包大小
      total_data_sent(sampler.total_data_sent_), // 截至该数据包发送时的总发送数据量
      total_data_sent_at_last_acked_packet(sampler.total_data_sent_at_last_acked_packet_), // 截至上一个确认的数据包时的总发送数据量
      last_acked_packet_sent_time(sampler.last_acked_packet_sent_time_), // 上一个确认的数据包的发送时间
      last_acked_packet_ack_time(sampler.last_acked_packet_ack_time_), // 上一个确认的数据包的确认时间
      total_data_acked_at_the_last_acked_packet(sampler.total_data_acked_), // 截至上一个确认的数据包时的总确认数据量
      is_app_limited(sampler.is_app_limited_) {} // 当前数据包发送时是否处于应用限制状态

BandwidthSampler::ConnectionStateOnSentPacket::ConnectionStateOnSentPacket()
    : sent_time(Timestamp::MinusInfinity()),
      size(DataSize::Zero()),
      total_data_sent(DataSize::Zero()),
      total_data_sent_at_last_acked_packet(DataSize::Zero()),
      last_acked_packet_sent_time(),
      last_acked_packet_ack_time(),
      total_data_acked_at_the_last_acked_packet(DataSize::Zero()),
      is_app_limited(false) {}

BandwidthSampler::ConnectionStateOnSentPacket::~ConnectionStateOnSentPacket() {}

}  // namespace bbr
}  // namespace webrtc
