#pragma once

#include <iostream>
#include <ostream>

namespace mm {

// Small output adapter used by GameServer and Battle. Tests/TCP can redirect it per command
// without touching the global std::cout buffer.
class OutputSink
{
public:
    explicit OutputSink(std::ostream& stream = std::cout) : stream_(&stream) {}

    std::ostream& stream() const { return *stream_; }

    void set_stream(std::ostream& stream) { stream_ = &stream; }

    template <typename T>
    const OutputSink& operator<<(const T& value) const
    {
        *stream_ << value;
        return *this;
    }

    explicit operator bool() const { return static_cast<bool>(*stream_); }

private:
    std::ostream* stream_;
};

class ScopedOutputRedirect
{
public:
    ScopedOutputRedirect(OutputSink& sink, std::ostream& stream)
      : sink_(sink), old_stream_(sink.stream())
    {
        sink_.set_stream(stream);
    }

    ~ScopedOutputRedirect() { sink_.set_stream(old_stream_); }

private:
    OutputSink& sink_;
    std::ostream& old_stream_;
};

}  // namespace mm
