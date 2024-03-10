/*******************************************************
 * @author      : dog head
 * @date        : Created in 2023/8/16 10:48 上午
 * @mail        : qw225967@github.com
 * @project     : .
 * @file        : bifrost_send_algorithm.h
 * @description : TODO
 *******************************************************/

#ifndef _BIFROST_SEND_ALGORITHM_H
#define _BIFROST_SEND_ALGORITHM_H

#include <memory>

#include "bifrost_send_algorithm_interface.h"
#include "bifrost_send_algorithm/quic_send_algorithm_adapter.h"
#include "bifrost_send_algorithm/tcc_client.h"
#include "quiche/quic/core/quic_types.h"

namespace bifrost {
typedef std::shared_ptr<BifrostSendAlgorithmInterface>
    BifrostSendAlgorithmInterfacePtr;

class BifrostSendAlgorithmManager : public TransportCongestionControlClient::Observer {
 public:
  BifrostSendAlgorithmManager(quic::CongestionControlType congestion_algorithm_type,
                       UvLoop** uv_loop);
  ~BifrostSendAlgorithmManager() override {
    algorithm_interface_.reset();
  }

 public:
  // TransportCongestionControlClient
  void OnTransportCongestionControlClientBitrates(
      TransportCongestionControlClient* tcc_client,
      TransportCongestionControlClient::Bitrates& bitrates) override {}
  void OnTransportCongestionControlClientSendRtpPacket(
      TransportCongestionControlClient* tcc_client, RtpPacket* packet,
      const webrtc::PacedPacketInfo& pacing_info) override {}

 public:
  void OnRtpPacketSend(RtpPacketPtr &rtp_packet, int64_t nowMs);
  bool OnReceiveRtcpFeedback(FeedbackRtpPacket* fb);
  void OnReceiveReceiverReport(webrtc::RTCPReportBlock report,
                               float rtt, int64_t nowMs);
  void UpdateRtt(float rtt);
  uint32_t get_pacing_rate();
  uint32_t get_congestion_windows();
  uint32_t get_bytes_in_flight();
  uint32_t get_pacing_transfer_time(uint32_t bytes);
  std::vector<double> get_trends();
  uint32_t get_avalibale_bitrate();
 private:
  BifrostSendAlgorithmInterfacePtr algorithm_interface_;
};

}  // namespace bifrost
#endif  //_BIFROST_SEND_ALGORITHM_H
