#include <EncodingPch.h>
#include <TiltedCore/ViewBuffer.hpp>
#include <catch2/catch.hpp>
#include "../common/DateTime.h"
#include <limits>

TEST_CASE("Authoritative calendar advances date and elapsed days across rest and year boundaries", "[world.calendar]")
{
    DateTime time;
    time.m_timeModel.Month = 11;
    time.m_timeModel.Day = 31;
    time.m_timeModel.Year = 201;
    time.m_timeModel.Time = 23.5f;
    time.m_timeModel.DaysPassed = 7000.5;
    REQUIRE(time.AdvanceHours(8));
    REQUIRE(time.m_timeModel.Year == 202);
    REQUIRE(time.m_timeModel.Month == 0);
    REQUIRE(time.m_timeModel.Day == 1);
    REQUIRE(time.m_timeModel.Time == 7.5f);
    REQUIRE(time.m_timeModel.DaysPassed == Approx(7000.5 + 8.0 / 24.0));
    const auto before = time;
    REQUIRE_FALSE(time.AdvanceHours(std::numeric_limits<double>::infinity()));
    REQUIRE_FALSE(time.AdvanceHours(-1));
    REQUIRE(time == before);
    REQUIRE(time.AdvanceHours(24.0 * 60));
    REQUIRE(time.m_timeModel.Month == 2);
    REQUIRE(time.m_timeModel.Day == 2);
}

TEST_CASE("Elapsed campaign days do not stop advancing on an old save", "[world.calendar]")
{
    DateTime time;
    time.m_timeModel.DaysPassed = 7000.0;
    const double before = time.m_timeModel.DaysPassed;
    for (size_t i = 0; i < 1000; ++i)
        time.Update(16);
    REQUIRE(time.m_timeModel.DaysPassed - before == Approx(16.0 * 20.0 / 86400.0).margin(1e-8));
}

TEST_CASE("Calendar prediction is independent of frame rate and handles a float rounding up at midnight", "[world.calendar]")
{
    DateTime coarse, fine;
    coarse.m_timeModel.Time = fine.m_timeModel.Time = 23.f;
    for (size_t i = 0; i < 1000; ++i)
        coarse.Update(1000);
    for (size_t i = 0; i < 100000; ++i)
        fine.Update(10);
    REQUIRE(coarse.m_timeModel.Time == fine.m_timeModel.Time);
    REQUIRE(coarse.m_timeModel.Day == fine.m_timeModel.Day);
    REQUIRE(coarse.m_timeModel.DaysPassed == Approx(fine.m_timeModel.DaysPassed).margin(1e-8));
    DateTime boundary;
    boundary.m_timeModel.Time = std::nextafter(24.f, 0.f);
    const uint32_t day = boundary.m_timeModel.Day;
    REQUIRE(boundary.AdvanceHours(0.000001));
    REQUIRE(boundary.m_timeModel.Time < 24.f);
    REQUIRE(boundary.m_timeModel.Day == day);
    REQUIRE(boundary.AdvanceHours(0.000001));
    REQUIRE(boundary.m_timeModel.Time >= 0.f);
    REQUIRE(boundary.m_timeModel.Time < 0.00001f);
    REQUIRE(boundary.m_timeModel.Day == day + 1);
}

TEST_CASE("Calendar wire rejects truncated, nonfinite and impossible dates", "[world.calendar]")
{
    TimeModel model;
    model.DaysPassed = 1234.56789;
    TiltedPhoques::Buffer buffer(64);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    model.Serialize(writer);
    TiltedPhoques::Buffer::Reader reader(&buffer);
    TimeModel decoded;
    decoded.Deserialize(reader);
    REQUIRE(decoded.IsValid());
    REQUIRE(decoded == model);
    for (size_t size = 0; size < writer.Size(); ++size)
    {
        TiltedPhoques::ViewBuffer view(buffer.GetWriteData(), size);
        TiltedPhoques::Buffer::Reader truncated(&view);
        TimeModel invalid;
        invalid.Deserialize(truncated);
        REQUIRE_FALSE(invalid.IsValid());
    }
    model.Month = 1;
    model.Day = 29;
    REQUIRE_FALSE(model.IsValid());
    model.Day = 0;
    REQUIRE_FALSE(model.IsValid());
    model.Day = 28;
    REQUIRE(model.IsValid());
    model.Time = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(model.IsValid());
}
