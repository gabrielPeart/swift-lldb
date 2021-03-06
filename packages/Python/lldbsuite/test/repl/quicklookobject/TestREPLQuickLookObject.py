# TestREPLQuickLookObject.py
#
# This source file is part of the Swift.org open source project
#
# Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See http://swift.org/LICENSE.txt for license information
# See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
#
# ------------------------------------------------------------------------------
"""Test that QuickLookObject works correctly in the REPL"""

import os, time
import unittest2
import lldb
from lldbsuite.test.lldbrepl import REPLTest, load_tests
import lldbsuite.test.lldbtest as lldbtest

class REPLQuickLookTestCase (REPLTest):

    mydir = REPLTest.compute_mydir(__file__)

    def doTest(self):
        self.command('_reflect(true).quickLookObject!', patterns=['Logical = true'])
        self.command('_reflect(1.25).quickLookObject!', patterns=['Double = 1.25'])
        self.command('_reflect(Float(1.25)).quickLookObject!', patterns=['Float = 1.25'])
        self.command('_reflect("Hello").quickLookObject!', patterns=['Text = \"Hello\"'])
        self.command('struct S {}; _reflect(S()).quickLookObject', patterns=['PlaygroundQuickLook\\? = nil'])


