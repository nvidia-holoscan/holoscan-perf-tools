#!/usr/bin/python3
#
# Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

import argparse
import csv
import math
import matplotlib.pyplot as plt
import numpy as np

parser = argparse.ArgumentParser(description='Graphs loopback-latency results.')
parser.add_argument('--file', metavar='path', type=str, required=True,
                   help='path to the data file to graph')
parser.add_argument('--frames', metavar='num', type=int, default=600,
                    help='number of frames to graph')
parser.add_argument('--first', metavar='num', type=int, default=1,
                    help='first frame to use for the graph')
parser.add_argument('--fps', metavar='rate', type=float, default=60,
                    help='framerate to use for reference intervals (disable: 0)')
parser.add_argument('--estimate', action='store_true', default=False,
                    help='when given, the data will be manipulated to provide an ' +
                         'estimate of the latency for a read + process + write use case')
parser.add_argument('--png', metavar='path', type=str,
                    help='path for the output PNG to write. If not provided, ' +
                         'pops up a window with the graph.')
parser.add_argument('--title', metavar='text', type=str,
                    help='text to use for the graph title')
args = parser.parse_args()

# Read the input CSV file.
rows = []
with open(args.file) as csvfile:
    reader = csv.reader(csvfile)
    for row in reader:
        rows.append(row)

# Extract the labels from the first row.
labels = rows[0][4:]

# Extract the frame numbers and times for the requested frames.
data = np.transpose(rows[args.first:args.frames + args.first])
frame_numbers = data[0]
times = np.array(data[4:], dtype=int)

# Determine the frame interval.
interval = 0
if args.fps > 0:
    interval = 1000000/args.fps
elif args.estimate:
    print('FPS must be given if "estimate" graph is requested.')
    exit(1)

colors = [[0.00, 0.27, 0.51, 0.8],
          [0.80, 0.30, 0.00, 0.8],
          [0.00, 0.43, 0.00, 0.8],
          [0.64, 0.00, 0.00, 0.8],
          [0.38, 0.20, 0.54, 0.8],
          [0.35, 0.14, 0.09, 0.8],
          [0.69, 0.27, 0.56, 0.8],
          [0.30, 0.30, 0.30, 0.8]]

# If requested, manipulate the data to provide an estimated read + process + write latency.
if args.estimate:
    labels = [labels[6], labels[7], labels[0], labels[1], labels[2], labels[3], labels[4], labels[5]]
    colors = [colors[6], colors[7], colors[0], colors[1], colors[2], colors[3], colors[4], colors[5]]
    times = np.array([times[6], times[7], times[0], times[1], times[2], times[3]])
    vsync_times = []
    for process_time in times.sum(axis=0):
        intervals = math.ceil(process_time / interval)
        vsync_times.append(int((intervals * interval) - process_time))
    vsync_times = np.array([vsync_times])
    times = np.append(times, vsync_times, axis=0)
    wire_times = np.full(vsync_times.shape, int(interval))
    times = np.append(times, wire_times, axis=0)

# Graph the data.
fig, ax = plt.subplots()
accum_times = 0
max_time = 0
for i in range(len(times)):
    if args.frames > 60:
        bar = ax.bar(frame_numbers, times[i], label=labels[i], bottom=accum_times,
                     align='edge', width=1.0, color=colors[i])
    else:
        bar = ax.bar(frame_numbers, times[i], label=labels[i], bottom=accum_times, color=colors[i])
    accum_times = times[i] if i is 0 else np.add(times[i], accum_times)
    max_time = accum_times.max()
    mean = int(times[i].mean())
    if (mean > 200):
        accum_mean = int(accum_times.mean())
        color = [max(0, v - 0.2) for v in bar[0].get_facecolor()]
        ax.axhline(accum_mean, color=color)
        ax.annotate('{}: {}\u00B5s ({}\u00B5s total)'.format(labels[i], mean, accum_mean),
                    xy=(0, accum_mean), backgroundcolor=color, color='white',
                    horizontalalignment='left', verticalalignment='center')
ax.set_ylabel('Time (\u00B5s)')
ax.set_xlabel('Frame')
handles, labels = ax.get_legend_handles_labels()
ax.legend(reversed(handles), reversed(labels), loc='upper right')
if args.frames > 60:
    ax.axes.xaxis.set_visible(False)
else:
    plt.xticks(rotation=90, va="center")
plt.title(args.title, fontsize=18)
plt.tight_layout()

# Add the reference frame lines.
if interval > 0:
    for i in range(int(max_time / interval) + (1 if args.estimate else 0)):
        ax.axhline((i + 1) * interval, linestyle=':', linewidth=4, color='black')

if args.png:
    fig.set_size_inches(12, 12)
    plt.savefig(args.png, bbox_inches='tight', pad_inches=0.3)
else:
    plt.show()
