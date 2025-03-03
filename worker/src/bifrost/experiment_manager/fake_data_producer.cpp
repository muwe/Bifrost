/*******************************************************
 * @author      : dog head
 * @date        : Created in 2023/6/14 10:04 上午
 * @mail        : qw225967@github.com
 * @project     : worker
 * @file        : data_producer.cpp
 * @description : TODO
 *******************************************************/

#include "bifrost/experiment_manager/fake_data_producer.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "utils.h"

namespace bifrost {
constexpr uint8_t kPacketWithH264[]{
    0x00, 0x04, 0x00, 0x01, 0x00, 0x06, 0xfa, 0x16, 0x3e, 0xa0, 0xf7, 0xcb,
    0x00, 0x00, 0x08, 0x00, 0x45, 0x00, 0x04, 0x21, 0x7e, 0x3c, 0x40, 0x00,
    0x40, 0x11, 0x20, 0x45, 0xc0, 0xa8, 0x13, 0x29, 0xac, 0x10, 0x18, 0x69,
    0x23, 0x28, 0xba, 0xf1, 0x04, 0x0d, 0x9c, 0x69, 0x90, 0x65, 0x16, 0x39,
    0x98, 0x52, 0x96, 0xa8, 0x1a, 0x04, 0x37, 0xe9, 0xbe, 0xde, 0x00, 0x04,
    0xe7, 0x31, 0x61, 0x30, 0x34, 0x33, 0x37, 0x65, 0x39, 0x42, 0xb5, 0xbc,
    0x6a, 0x61, 0x00, 0x00, 0x5c, 0x01, 0xbd, 0xc3, 0x24, 0x33, 0x7e, 0x29,
    0x46, 0x39, 0x54, 0x7f, 0xff, 0xa0, 0x88, 0x0e, 0x63, 0x73, 0x13, 0x11,
    0x4f, 0x7a, 0xd9, 0x0a, 0xd3, 0xf2, 0x9c, 0xd5, 0x17, 0x82, 0x85, 0xee,
    0x2f, 0xfd, 0x1b, 0xdd, 0xd2, 0x94, 0x3e, 0xa5, 0x67, 0xb3, 0x09, 0xc1,
    0x60, 0xbe, 0x27, 0xc4, 0xae, 0x16, 0xf1, 0x06, 0x43, 0x6f, 0x35, 0xe9,
    0x58, 0xb7, 0x2c, 0x9b, 0x3f, 0x26, 0x08, 0x81, 0xc4, 0xc4, 0x29, 0xa5,
    0xa9, 0x94, 0x85, 0x08, 0x21, 0xc4, 0x3f, 0xc6, 0x8d, 0x0a, 0x52, 0x70,
    0x8e, 0xe6, 0x24, 0xa1, 0x44, 0x32, 0x98, 0xdb, 0x6a, 0xb7, 0x03, 0x3f,
    0x1e, 0x59, 0x15, 0xdc, 0x38, 0xae, 0xb6, 0x0d, 0x19, 0x2c, 0x28, 0x37,
    0x9c, 0x84, 0x1f, 0x8c, 0x9c, 0x65, 0xbc, 0x9c, 0x7a, 0x5d, 0x6e, 0x18,
    0x0e, 0xb5, 0x83, 0xe6, 0x18, 0xb4, 0xd6, 0x04, 0xe8, 0xaf, 0x6f, 0x3a,
    0x87, 0x6c, 0x87, 0xe9, 0xed, 0x2b, 0x41, 0x6b, 0xdb, 0x02, 0x69, 0xd5,
    0xc6, 0x06, 0xe1, 0xc4, 0x65, 0x9e, 0xcb, 0x0b, 0x48, 0x1a, 0xe6, 0xf3,
    0xda, 0xfe, 0x33, 0x14, 0x06, 0xc0, 0x1d, 0xa5, 0xd8, 0x96, 0xf2, 0xae,
    0x12, 0xaa, 0xe1, 0x28, 0x6a, 0x04, 0x56, 0xcc, 0xad, 0xd9, 0x1e, 0x27,
    0x75, 0x5d, 0xe0, 0x75, 0x55, 0xe9, 0xb7, 0xa5, 0xbf, 0xd1, 0x9e, 0xfd,
    0xdf, 0xad, 0xb1, 0x9c, 0x41, 0xef, 0xc7, 0xf4, 0xf9, 0x95, 0xd6, 0xde,
    0xd2, 0x12, 0x0a, 0xa8, 0x9c, 0x00, 0x1c, 0x71, 0xb8, 0x92, 0x59, 0xa5,
    0x60, 0x60, 0x43, 0x9e, 0x8f, 0x30, 0x69, 0x31, 0x85, 0xa1, 0xaa, 0xe1,
    0x7e, 0xec, 0x7e, 0x46, 0x98, 0xf0, 0x15, 0x29, 0x02, 0x59, 0x0e, 0xc4,
    0x84, 0x53, 0x4e, 0xe4, 0x1f, 0xca, 0x84, 0x2a, 0xe0, 0x2c, 0x73, 0x9b,
    0x60, 0xc2, 0x3b, 0x9f, 0xde, 0x83, 0x76, 0x5f, 0x3b, 0x77, 0xc2, 0xac,
    0x0d, 0x05, 0xd3, 0xf6, 0x86, 0x0f, 0xa2, 0xb0, 0xd0, 0xaf, 0x29, 0x3e,
    0xbe, 0xf7, 0x25, 0xf4, 0x08, 0x92, 0x7e, 0xf8, 0x25, 0x40, 0x28, 0xf0,
    0xc1, 0x74, 0x86, 0x9e, 0xce, 0x47, 0xe7, 0x79, 0x29, 0x80, 0xde, 0xd0,
    0x1d, 0x0d, 0x3b, 0x44, 0x05, 0x27, 0xa4, 0xae, 0xd7, 0x91, 0x12, 0xb9,
    0x5b, 0xda, 0x1b, 0xfb, 0xe6, 0x1b, 0xd1, 0x35, 0xd1, 0x7e, 0x37, 0x2d,
    0x53, 0x11, 0xc6, 0x77, 0x99, 0x95, 0x7a, 0xfa, 0x9c, 0x38, 0x65, 0x1e,
    0x81, 0xe1, 0x15, 0x6f, 0xca, 0xd9, 0xbe, 0x15, 0x22, 0xa3, 0x45, 0x01,
    0x3e, 0x00, 0x03, 0xf7, 0xca, 0x51, 0xc1, 0xb7, 0xf5, 0x3a, 0x6d, 0x9d,
    0xd3, 0x11, 0x64, 0x48, 0x50, 0x67, 0xc6, 0x3d, 0x9d, 0x2c, 0xd0, 0xd0,
    0x2f, 0x9d, 0xa0, 0x01, 0x45, 0xcd, 0x98, 0x12, 0x9a, 0xc2, 0x69, 0x69,
    0x31, 0xec, 0x25, 0x4c, 0xa6, 0xab, 0xc8, 0x84, 0x56, 0xc4, 0xa7, 0xe9,
    0x60, 0x1d, 0xb4, 0x69, 0x59, 0xdf, 0x77, 0xcc, 0xa3, 0x57, 0x7e, 0xc6,
    0x82, 0x43, 0xb3, 0x92, 0xc1, 0x1d, 0x0f, 0xea, 0x0f, 0x18, 0x31, 0x9d,
    0x7c, 0xb9, 0x25, 0xbe, 0x65, 0x4a, 0x5d, 0x6a, 0xb0, 0xea, 0xf0, 0x87,
    0x0f, 0x47, 0xe4, 0xdf, 0x39, 0xbe, 0x48, 0x63, 0x54, 0x47, 0x8e, 0x86,
    0xc0, 0x73, 0x28, 0x23, 0x8e, 0x90, 0x12, 0x3d, 0x44, 0x8a, 0x5f, 0xeb,
    0xc5, 0x7d, 0xef, 0x89, 0xb9, 0xa4, 0x77, 0xd6, 0x56, 0x93, 0xf0, 0x4f,
    0x64, 0x1f, 0x4b, 0x1a, 0x43, 0x13, 0x29, 0xbc, 0xe0, 0x6a, 0x60, 0xe0,
    0x98, 0xd2, 0x8d, 0x51, 0xba, 0x28, 0xa7, 0xc3, 0x25, 0x77, 0xf7, 0x87,
    0x58, 0xf1, 0x72, 0xb1, 0x17, 0x5b, 0xe9, 0xd0, 0xca, 0x78, 0x34, 0xd7,
    0x77, 0x3a, 0x8b, 0xcb, 0x45, 0xf5, 0x5c, 0x44, 0x8d, 0xea, 0x57, 0x91,
    0xc0, 0x08, 0x5a, 0x7b, 0x91, 0xed, 0x17, 0xac, 0x17, 0x16, 0xff, 0xca,
    0x01, 0xf8, 0x2c, 0x2e, 0x2d, 0xab, 0x8b, 0xa8, 0x57, 0xd7, 0x77, 0xb1,
    0x73, 0xe8, 0xee, 0x00, 0x95, 0xcb, 0xa1, 0xcd, 0xa2, 0x14, 0x86, 0x33,
    0xe4, 0x84, 0xf2, 0xa8, 0x6c, 0x80, 0x56, 0x29, 0x32, 0x47, 0x5f, 0x56,
    0xac, 0x40, 0xc8, 0x45, 0x55, 0x54, 0x38, 0x13, 0x00, 0x61, 0x8e, 0xe2,
    0xda, 0x6e, 0x28, 0x21, 0x0e, 0x8b, 0x9c, 0x1c, 0xb8, 0xee, 0xf7, 0x43,
    0xc1, 0xd6, 0x45, 0x77, 0x8c, 0x0a, 0xdc, 0xcf, 0xb1, 0x04, 0xa8, 0xaf,
    0xbd, 0x62, 0xb4, 0x2f, 0x48, 0x20, 0x15, 0xcc, 0x11, 0x37, 0x40, 0xbf,
    0xe2, 0x77, 0xf6, 0x5a, 0xe5, 0x26, 0x1d, 0x6a, 0x16, 0x75, 0xc7, 0x4d,
    0xa9, 0x7c, 0xe2, 0xea, 0x0b, 0x32, 0xf2, 0x4e, 0xac, 0x10, 0xb2, 0x23,
    0xe9, 0xf1, 0xe7, 0x26, 0x3c, 0x65, 0x19, 0x2d, 0x35, 0x62, 0x34, 0xf7,
    0x89, 0x0e, 0x9c, 0xd7, 0x18, 0xee, 0x48, 0xcd, 0x5f, 0x5f, 0xe8, 0x84,
    0x24, 0x5b, 0xec, 0x82, 0xf8, 0x4c, 0xe9, 0x6b, 0x53, 0x71, 0xa0, 0x66,
    0x88, 0xc6, 0xa8, 0xf5, 0x4b, 0xb7, 0x49, 0xdc, 0x59, 0x19, 0xcf, 0xed,
    0xda, 0x69, 0x51, 0x30, 0x49, 0xd7, 0x02, 0xea, 0x3f, 0x8e, 0xd8, 0x78,
    0xc6, 0x1c, 0xc2, 0x32, 0x3c, 0x1f, 0xdc, 0xb1, 0xbd, 0x7d, 0x44, 0x7c,
    0x5d, 0x22, 0xd0, 0xa7, 0x19, 0x94, 0xf3, 0x42, 0x0c, 0x74, 0x9f, 0x30,
    0xd4, 0x25, 0x89, 0x0d, 0x5f, 0xaa, 0xc0, 0xc6, 0x63, 0x57, 0x0a, 0x25,
    0x20, 0x39, 0x0d, 0x4f, 0x35, 0x02, 0xda, 0x76, 0x09, 0xd4, 0x68, 0x03,
    0xe7, 0x8c, 0xfe, 0x51, 0xa8, 0xb6, 0xc2, 0xab, 0xa3, 0x3b, 0xee, 0xed,
    0xdc, 0x6a, 0x67, 0x3c, 0x93, 0xd6, 0xbc, 0xc1, 0x58, 0xae, 0x71, 0x3f,
    0x6a, 0xc6, 0x25, 0x31, 0xa1, 0xa5, 0xdb, 0xb3, 0x91, 0xfa, 0x3e, 0xd9,
    0x78, 0xaf, 0x0b, 0x24, 0xd1, 0xf8, 0x3a, 0x1e, 0xfb, 0x28, 0x25, 0xaa,
    0x31, 0x8a, 0x92, 0x02, 0x0e, 0xda, 0xcc, 0x46, 0xb2, 0x62, 0xf7, 0x81,
    0x4e, 0x91, 0x4f, 0x86, 0x0a, 0x94, 0x4a, 0x24, 0xf2, 0xaf, 0xda, 0xe5,
    0x97, 0xfc, 0xea, 0x53, 0xe2, 0xcc, 0x32, 0x09, 0xa9, 0x1e, 0x8d, 0xa6,
    0x5e, 0x65, 0xb0, 0x97, 0xc0, 0x0c, 0xab, 0x33, 0x77, 0x61, 0xdd, 0xdb,
    0x90, 0x47, 0xf8, 0xee, 0xbf, 0x48, 0xc8, 0x4f, 0x44, 0xda, 0xf9, 0x3e,
    0xaa, 0x4e, 0x6b, 0x58, 0x77, 0x1c, 0xce, 0x4f, 0xe1, 0xd9, 0xb9, 0x6c,
    0x55, 0x8e, 0xdc, 0xf0, 0x68, 0x41, 0x9d, 0x8c, 0x5b, 0x4b, 0xd6, 0x52,
    0x06, 0x2d, 0x66, 0x87, 0xcd, 0x3a, 0x71, 0xc6, 0x84, 0xed, 0x4d, 0xc8,
    0x7c, 0x36, 0x70, 0x5c, 0xee, 0xeb, 0x8e, 0xea, 0x65, 0x87, 0x68, 0x89,
    0x0d, 0x02, 0x74, 0x25, 0x12, 0xf2, 0x1b, 0x45, 0xa2, 0xc6, 0x42, 0x94,
    0x5c, 0x23, 0x8e, 0x20, 0xc6, 0xc3, 0x05, 0x9a, 0xa9, 0x7b, 0x6c, 0x11,
    0xd9, 0x37, 0xb3, 0x25, 0x0a, 0xde, 0xc8, 0x53, 0x7d, 0xa3, 0x8c, 0x52,
    0x89, 0x83, 0xfc, 0xf5, 0xd6, 0x25, 0x2e, 0xa5, 0x1b, 0x1a, 0xfb, 0xec,
    0xfb, 0xdf, 0x01, 0x78, 0xce, 0x0a, 0x54, 0x06, 0x59, 0x8d, 0x7e, 0xac,
    0x55, 0xbf, 0x0c, 0x72, 0x46, 0x47, 0x4c, 0x61, 0x55, 0x2e, 0xe2, 0xa4,
    0x55, 0xe1, 0xfa, 0x0e, 0x7d, 0x1c, 0x96, 0x94, 0x04, 0x37, 0xab, 0x21,
    0x0d, 0x23, 0xd2, 0x5d, 0x9d, 0x74, 0x44, 0xbe, 0x11, 0x31, 0xcd, 0x20,
    0xce, 0xbd, 0xfb, 0xa1, 0xeb};

FakeDataProducer::FakeDataProducer(uint32_t ssrc) : ssrc_(ssrc), sequence_(1) {
}

FakeDataProducer::~FakeDataProducer() {
#ifdef USING_LOCAL_FILE_DATA
  data_file_.close();
#endif
}

RtpPacketPtr FakeDataProducer::CreateData() {
  // 使用webrtc中rtp包初始化方式
  auto* send_packet =
      new webrtc::RtpPacketToSend(nullptr, sizeof(kPacketWithH264) + RtpPacket::HeaderSize);
#ifdef USING_LOCAL_FILE_DATA
  data_file_.read((char*)data, sizeof(kPacketWithH264));
#endif
  send_packet->SetSequenceNumber(this->sequence_++);
  send_packet->SetPayloadType(101);
  send_packet->SetSsrc(this->ssrc_);
  memcpy(send_packet->Buffer().data() + RtpPacket::HeaderSize, kPacketWithH264, sizeof(kPacketWithH264));

  send_packet->SetPayloadSize(sizeof(kPacketWithH264));

  // 转回 rtp packet
  auto len = send_packet->capacity();
  auto* payload_data = new uint8_t[len];
  memcpy(payload_data, send_packet->data(), len);
  RtpPacketPtr rtp_packet = RtpPacket::Parse(payload_data, len);
  // mediasoup parse 内部只new了包结构，没有new payload空间，payload空间使用了一个共享的静态区域
  rtp_packet->SetPayloadDataPtr(&payload_data);

  this->GetRtpExtensions(rtp_packet.get());

  return rtp_packet;
}

void FakeDataProducer::GetRtpExtensions(RtpPacket* packet) {
  static uint8_t buffer[4096];
  uint8_t extenLen = 2u;
  static std::vector<RtpPacket::GenericExtension> extensions;
  // This happens just once.
  if (extensions.capacity() != 24) extensions.reserve(24);

  extensions.clear();

  uint8_t* bufferPtr{buffer};
  // NOTE: Add value 0. The sending Transport will update it.
  uint16_t wideSeqNumber{0u};

  Byte::set_2_bytes(bufferPtr, 0, wideSeqNumber);
  extensions.emplace_back(static_cast<uint8_t>(7), extenLen, bufferPtr);
  packet->SetExtensions(2, extensions);
  packet->SetTransportWideCc01ExtensionId(7);
}
}  // namespace bifrost