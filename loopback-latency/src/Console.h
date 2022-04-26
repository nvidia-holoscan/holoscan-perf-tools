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

#pragma once

#include <iostream>

namespace ConsoleColors
{

const std::string Red("\033[0;31m");
const std::string Green("\033[0;32m");
const std::string Yellow("\033[0;33m");
const std::string Blue("\033[0;34m");
const std::string Magenta("\033[0;35m");
const std::string Cyan("\033[0;36m");
const std::string Reset("\033[0m");

}

#define ErrorColor(x)    ConsoleColors::Red << x << ConsoleColors::Reset
#define WarningColor(x)  ConsoleColors::Yellow << x << ConsoleColors::Reset
#define SuccessColor(x)  ConsoleColors::Green << x << ConsoleColors::Reset
#define ProducerColor(x) ConsoleColors::Cyan << x << ConsoleColors::Reset
#define ConsumerColor(x) ConsoleColors::Magenta << x << ConsoleColors::Reset

#define Error(x)   std::cerr << ErrorColor("ERROR: " << x) << std::endl
#define Warning(x) std::cerr << WarningColor("WARNING: " << x) << std::endl
#define Log(x)     std::cout << x << std::endl
