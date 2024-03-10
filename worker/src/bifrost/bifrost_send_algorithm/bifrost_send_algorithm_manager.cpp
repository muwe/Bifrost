/*******************************************************
 * @author      : dog head
 * @date        : Created in 2023/8/16 10:49 上午
 * @mail        : qw225967@github.com
 * @project     : .
 * @file        : bifrost_send_algorithm.cpp
 * @description : TODO
 *******************************************************/

#include "bifrost_send_algorithm/bifrost_send_algorithm_manager.h"
#include "rtcp_feedback.h"

namespace bifrost {
const uint32_t InitialAvailableGccBitrate = 400000u;

BifrostSendAlgorithmManager::BifrostSendAlgorithmManager(
    quic::CongestionControlType congestion_algorithm_type, UvLoop** uv_loop) {
  switch (congestion_algorithm_type) {
    case quic::kCubicBytes:
    case quic::kRenoBytes:
    case quic::kBBR:
    case quic::kPCC:
    case quic::kBBRv2:
      this->algorithm_interface_ = std::make_shared<QuicSendAlgorithmAdapter>(uv_loop, congestion_algorithm_type);
      break;
    case quic::kGoogCC:
    case quic::kBBRvWebrtc:
      this->algorithm_interface_ = std::make_shared<TransportCongestionControlClient>(
          this, congestion_algorithm_type, InitialAvailableGccBitrate, uv_loop);
      break;
  }
}

void BifrostSendAlgorithmManager::OnRtpPacketSend(RtpPacketPtr &rtp_packet, int64_t nowMs) {
  this->algorithm_interface_->OnRtpPacketSend(rtp_packet, nowMs);
}

bool BifrostSendAlgorithmManager::OnReceiveRtcpFeedback(FeedbackRtpPacket* fb) {
  return this->algorithm_interface_->OnReceiveRtcpFeedback(fb);
}

void BifrostSendAlgorithmManager::OnReceiveReceiverReport(webrtc::RTCPReportBlock report,
                                                                 float rtt, int64_t nowMs) {
  this->algorithm_interface_->OnReceiveReceiverReport(report, rtt, nowMs);
}

void BifrostSendAlgorithmManager::UpdateRtt(float rtt) {
  this->algorithm_interface_->UpdateRtt(rtt);
}

uint32_t BifrostSendAlgorithmManager::get_pacing_rate() { 
  return algorithm_interface_->get_pacing_rate(); 
}

uint32_t BifrostSendAlgorithmManager::get_congestion_windows() { 
  return algorithm_interface_->get_congestion_windows(); 
}

uint32_t BifrostSendAlgorithmManager::get_bytes_in_flight() { 
  return algorithm_interface_->get_bytes_in_flight(); 
}

uint32_t BifrostSendAlgorithmManager::get_pacing_transfer_time(uint32_t bytes) {
  return algorithm_interface_->get_pacing_transfer_time(bytes); 
}

std::vector<double> BifrostSendAlgorithmManager::get_trends() {
  return algorithm_interface_->get_trends(); 
}

uint32_t BifrostSendAlgorithmManager::get_avalibale_bitrate() {
  return algorithm_interface_->get_avalibale_bitrate(); 
}
}  // namespace bifrost