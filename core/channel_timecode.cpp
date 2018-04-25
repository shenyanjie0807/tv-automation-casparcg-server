/*
 * Copyright (c) 2011 Sveriges Television AB <info@casparcg.com>
 *
 * This file is part of CasparCG (www.casparcg.com).
 *
 * CasparCG is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CasparCG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CasparCG. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 */

#include "channel_timecode.h"
#include "common/log.h"

#include <boost/lexical_cast.hpp>
#include <chrono>

namespace caspar { namespace core {
    
class timecode_source_proxy : public timecode_source
{
  public:
    timecode_source_proxy(const int index, const std::shared_ptr<timecode_source>& src)
        : index_(index)
        , src_(src)
        , is_valid_(true)
    {
    }

    const frame_timecode& timecode() override
    {
        if (!is_valid_)
            return frame_timecode::empty();

        const std::shared_ptr<timecode_source> src = src_.lock();
        if (src)
            return src->timecode();

        CASPAR_LOG(warning) << L"timecode[" << index_ << L"] - Lost timecode source";
        is_valid_ = false;
        return frame_timecode::empty();
    }

    bool has_timecode() override
    {
        if (!is_valid_)
            return false;

        const std::shared_ptr<timecode_source> src = src_.lock();
        if (src)
            return src->has_timecode();

        CASPAR_LOG(warning) << L"timecode[" << index_ << L"] - Lost timecode source";
        is_valid_ = false;
        return false;
    }

    bool provides_timecode() override
    {
        if (!is_valid_)
            return false;

        const std::shared_ptr<timecode_source> src = src_.lock();
        if (src)
            return src->provides_timecode();

        CASPAR_LOG(warning) << L"timecode[" << index_ << L"] - Lost timecode source";
        is_valid_ = false;
        return false;
    }

    std::wstring print() const override
    {
        if (!is_valid_)
            return L"";

        const std::shared_ptr<timecode_source> src = src_.lock();
        if (src)
            return src->print();

        return L"";
    }

    const int                      index_;
    std::weak_ptr<timecode_source> src_;
    bool                           is_valid_;
};

struct channel_timecode::impl
{
  public:
    explicit impl(const int index, const video_format_desc& format)
        : timecode_(frame_timecode(0, static_cast<uint8_t>(round(format.fps))))
        , format_(format)
        , index_(index)
        , is_system_clock_(false)
        , clock_offset_(0)
    {
    }

    void start() { update_offset(core::frame_timecode::empty()); }

    frame_timecode tick()
    {
        if (!is_free()) {
            const frame_timecode tc = source_->timecode();
            if (tc.is_valid()) {
                timecode_ = tc; // TODO - adjust to match fps
                update_offset(tc);
                return timecode_;
            }

            // fall back to incrmenting
            CASPAR_LOG(warning) << L"timecode[" << index_ << L"] - Timecode update invalid. Ignoring";
        }

        static long millis_per_day = 1000 * 60 * 60 * 24;

        const long millis = (time_now() - clock_offset_) % millis_per_day;
        const long frames = static_cast<long>(round(millis / (1000 / format_.fps)));

        return timecode_ = frame_timecode(frames, static_cast<uint8_t>(round(format_.fps)));
    }

    frame_timecode timecode() const { return timecode_; }
    void           timecode(frame_timecode& tc)
    {
        if (is_free()) {
            update_offset(tc);
            timecode_ = tc;
        }
    }

    void change_format(const video_format_desc& format)
    {
        format_ = format;

        // TODO - update current to match fps
    }

    bool is_free() const { return !(source_ && source_->has_timecode()); }

    bool set_source(const std::shared_ptr<core::timecode_source>& src)
    {
        if (!src->provides_timecode())
            return false;

        source_ = src;
        is_system_clock_ = false;
        CASPAR_LOG(info) << L"timecode[" << index_ << L"] - Loaded producer " << src->print();
        return true;
    }
    bool set_weak_source(const std::shared_ptr<core::timecode_source>& src)
    {
        if (!src->provides_timecode())
            return false;

        source_          = std::make_shared<timecode_source_proxy>(index_, src);
        is_system_clock_ = false;
        CASPAR_LOG(info) << L"timecode[" << index_ << L"] - Loaded producer " << src->print();
        return true;
    }
    void clear_source()
    {
        source_          = nullptr;
        is_system_clock_ = false;
        CASPAR_LOG(info) << L"timecode[" << index_ << L"] - Set to freerun";
    }
    void set_system_time()
    {
        // TODO timezone / custom offset
        clear_source();
        clock_offset_ = 0;
        is_system_clock_ = true;
        CASPAR_LOG(info) << L"timecode[" << index_ << L"] - Set to system clock";
    }

    std::wstring source_name() const
    {
        if (source_)
            return source_->print();

        if (is_system_clock_)
            return L"clock";

        return L"";
    }

  private:
    void update_offset(core::frame_timecode tc)
    {
        clock_offset_ = time_now() - tc.pts();
        is_system_clock_ = false;
    }

    int64_t time_now() const
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    frame_timecode    timecode_;
    video_format_desc format_;
    const int         index_;

    std::shared_ptr<core::timecode_source> source_; // TODO - this needs some lock
    bool                                   is_system_clock_;
    int64_t                                clock_offset_;
};

channel_timecode::channel_timecode(int index, const video_format_desc& format)
    : impl_(new impl(index, format))
{
}

void           channel_timecode::start() { impl_->start(); }
frame_timecode channel_timecode::tick() { return impl_->tick(); }

frame_timecode channel_timecode::timecode() const { return impl_->timecode(); }
void           channel_timecode::timecode(frame_timecode& tc) { impl_->timecode(tc); }

void channel_timecode::change_format(const video_format_desc& format) { impl_->change_format(format); }

bool channel_timecode::is_free() const { return impl_->is_free(); }
std::wstring channel_timecode::source_name() const { return impl_->source_name(); }

bool channel_timecode::set_source(std::shared_ptr<core::timecode_source> src) { return impl_->set_source(src); }
bool channel_timecode::set_weak_source(std::shared_ptr<core::timecode_source> src) { return impl_->set_weak_source(src); }
void channel_timecode::clear_source() { impl_->clear_source(); }
void channel_timecode::set_system_time() { impl_->set_system_time(); }

}} // namespace caspar::core