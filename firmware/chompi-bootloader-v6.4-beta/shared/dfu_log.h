/** Small module for creating a log of DFU events for debug and validation */
#pragma once
#include <cstdint>

#include "daisy.h"

struct DfuLogger {
  struct Event {
    enum class Type { Status, Read, Write, Erase, WriteBusy, EraseBusy, Unknown };

    const char* getStringForType(Type t) {
      switch (t) {
        case Type::Status:
          return "Status";
        case Type::Read:
          return "Read";
        case Type::Write:
          return "Write";
        case Type::Erase:
          return "Erase";
        case Type::WriteBusy:
          return "WriteBusy";
        case Type::EraseBusy:
          return "EraseBusy";
        case Type::Unknown:
          return "Unknown";
      }
    }

    Event()
        : type(Type::Unknown),
          timestamp(0),
          start_addr(0),
          length(0),
          duration(0) {}

    Type type;
    uint32_t timestamp;
    uint32_t start_addr;
    uint32_t length;
    uint32_t duration;
  };

  daisy::FIFO<Event, 512> eventList;

  /** Clears all content from eventList */
  inline void Clear() {
    eventList.Clear();
  }

  inline Event popEvent() { return eventList.PopFront(); }

  inline bool pushEvent(Event::Type t, uint32_t timestamp, uint32_t addr, uint32_t len,
                        uint32_t dur) {
    Event e;
    e.type = t;
    e.timestamp = timestamp;
    e.start_addr = addr;
    e.length = len;
    e.duration = dur;

    return eventList.PushBack(e);
  }

  inline uint32_t getEventCount() const { return eventList.GetNumElements(); }
};
