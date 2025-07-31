#!/usr/bin/env python3

import numpy as np
import enum
from droneloc import kalman_type

KALMAN_TYPE = kalman_type.UKF6

# Distances from DWM1001-server
MCAST_GRP = '224.1.1.1'
MCAST_PORT = 5555

# Telemetry from drone
UDP_TELEMETRY_IP = '127.0.0.1'
UDP_TELEMETRY_PORT = 5556

# Commands to drone
UDP_COMMANDS_IP = '127.0.0.1'
UDP_COMMANDS_PORT = 5557

# Calculated math output to plot
UDP_PLOT_IP = '127.0.0.1'
UDP_PLOT_PORT = 5558

#
# Testing stand
#

# Drone coords are in ENU, this is the angle in radians of how (Y-Axis points North, X-Axis points East)
# the landing area is oriented, the rotation clockwise
LANDING_ANGLE = 0

LENGTH = 1.300 #side length assuming anchor layout is a square
SQUARE_DIAGONAL = ((LENGTH**2) + (LENGTH**2))**0.5

# Anchors coords (should create a rectangle with clockwise coords)
ANCHORS = {
    # Network 0xdeca, tag 0x51b3
    0xd59b: [0.000, 0.000, 0.000], #Master Anchor at Origin
    0x4936: [LENGTH, 0.000, 0.000], #Slave 3
    0x852c: [LENGTH, LENGTH, 0.000], #Slave 2
    0xda9f: [0.000, LENGTH, 0.000], #Slave 1

    #0x11cf: [0.650, 0.000, 0.000],
    #0x1337: [1.300, 0.650, 0.000],
    #0x14c6: [0.650, 1.300, 0.000],
    #0x14d9: [0.000, 0.650, 0.000], # master node
}

#
# Tags
#
TAG_ADDRS = [0x51b3]
#
# Nano33BLE
#
NANO33_MAC = "e6:13:3e:cf:ea:78"
