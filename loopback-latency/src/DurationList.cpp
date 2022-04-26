/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "DurationList.h"

void DurationList::Append(const Microseconds& d)
{
    m_values.push_back(d);
}

void DurationList::Append(const TimePoint& a, const TimePoint& b)
{
    m_values.push_back(std::chrono::duration_cast<Microseconds>(b - a));
}

size_t DurationList::Size() const
{
    return m_values.size();
}

Microseconds DurationList::Min() const
{
    return *std::min_element(m_values.begin(), m_values.end());
}

Microseconds DurationList::Max() const
{
    return *std::max_element(m_values.begin(), m_values.end());
}

Microseconds DurationList::Avg() const
{
    if (m_values.size() == 0)
        return Microseconds(0);

    uint64_t total = 0;
    for (const auto& value : m_values)
        total += value.count();
    return Microseconds(total / m_values.size());
}

std::string DurationList::Summary() const
{
    std::ostringstream ss;
    ss << "avg = " << std::setw(6) << Avg().count() << ", "
       << "min = " << std::setw(6) << Min().count() << ", "
       << "max = " << std::setw(6) << Max().count();
    return ss.str();
}

std::string DurationList::SummaryInFrameIntervals(const Microseconds& frameInterval) const
{
    std::ostringstream ss;
    ss << std::setprecision(3)
       << "avg = " << std::setw(6) << (float)Avg().count() / frameInterval.count() << ", "
       << "min = " << std::setw(6) << (float)Min().count() / frameInterval.count() << ", "
       << "max = " << std::setw(6) << (float)Max().count() / frameInterval.count();
    return ss.str();
}
