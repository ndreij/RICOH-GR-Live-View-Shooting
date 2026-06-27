#include "mjpeg_stream.h"

bool MjpegStream::begin(uint8_t* frameBuf, size_t cap, FrameCallback cb, void* user) {
  if (frameBuf == nullptr || cap < 4 || cb == nullptr) {
    _frameBuf = nullptr;
    _cap = 0;
    _callback = nullptr;
    reset();
    return false;
  }

  _frameBuf = frameBuf;
  _cap = cap;
  _callback = cb;
  _user = user;
  _frames = 0;
  _droppedFrames = 0;
  reset();
  return true;
}

void MjpegStream::reset() {
  _len = 0;
  _state = State::SeekingSoi;
  _prev = 0;
  _havePrev = false;
  _overflow = false;
}

size_t MjpegStream::process(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0 || _frameBuf == nullptr || _callback == nullptr) {
    return 0;
  }

  const uint32_t before = _frames;

  for (size_t i = 0; i < len; ++i) {
    const uint8_t value = data[i];
    const bool sawMarker = _havePrev && _prev == 0xFF;

    if (_state == State::SeekingSoi) {
      if (sawMarker && value == 0xD8) {
        startFrame();
      }
    } else {
      appendByte(value);
      if (sawMarker && value == 0xD9) {
        finishFrame();
      }
    }

    _prev = value;
    _havePrev = true;
  }

  return _frames - before;
}

uint32_t MjpegStream::frames() const {
  return _frames;
}

uint32_t MjpegStream::droppedFrames() const {
  return _droppedFrames;
}

size_t MjpegStream::currentLength() const {
  return _len;
}

void MjpegStream::startFrame() {
  _state = State::InFrame;
  _len = 0;
  _overflow = false;

  appendByte(0xFF);
  appendByte(0xD8);
}

void MjpegStream::appendByte(uint8_t value) {
  if (_overflow) {
    return;
  }
  if (_len >= _cap) {
    _overflow = true;
    return;
  }
  _frameBuf[_len++] = value;
}

void MjpegStream::finishFrame() {
  if (_overflow || _len < 20) {
    dropFrame();
    return;
  }

  _callback(_frameBuf, _len, _user);
  ++_frames;

  _state = State::SeekingSoi;
  _len = 0;
  _overflow = false;
}

void MjpegStream::dropFrame() {
  ++_droppedFrames;
  _state = State::SeekingSoi;
  _len = 0;
  _overflow = false;
}
