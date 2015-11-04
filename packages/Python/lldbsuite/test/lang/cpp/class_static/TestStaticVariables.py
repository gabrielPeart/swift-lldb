"""
Test display and Python APIs on file and class static variables.
"""

from __future__ import print_function



import os, time
import lldb
from lldbsuite.test.lldbtest import *
import lldbsuite.test.lldbutil as lldbutil

class StaticVariableTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line number to break at.
        self.line = line_number('main.cpp', '// Set break point at this line.')

    @expectedFailureWindows("llvm.org/pr24764")
    def test_with_run_command(self):
        """Test that file and class static variables display correctly."""
        self.build()
        self.runCmd("file a.out", CURRENT_EXECUTABLE_SET)

        lldbutil.run_break_set_by_file_and_line (self, "main.cpp", self.line, num_expected_locations=1, loc_exact=True)

        self.runCmd("run", RUN_SUCCEEDED)

        # The stop reason of the thread should be breakpoint.
        self.expect("thread list", STOPPED_DUE_TO_BREAKPOINT,
            substrs = ['stopped',
                       'stop reason = breakpoint'])

        # global variables are no longer displayed with the "frame variable" command. 
        self.expect('target variable A::g_points', VARIABLES_DISPLAYED_CORRECTLY,
            patterns=['\(PointType \[[1-9]*\]\) A::g_points = {.*}'])
        self.expect('target variable g_points', VARIABLES_DISPLAYED_CORRECTLY,
            substrs = ['(PointType [2]) g_points'])

        # On Mac OS X, gcc 4.2 emits the wrong debug info for A::g_points.
        # A::g_points is an array of two elements.
        if self.platformIsDarwin() or self.getPlatform() == "linux":
            self.expect("target variable A::g_points[1].x", VARIABLES_DISPLAYED_CORRECTLY,
                startstr = "(int) A::g_points[1].x = 11")

    @expectedFailureDarwin(9980907)
    @expectedFailureClang('Clang emits incomplete debug info.')
    @expectedFailureFreeBSD('llvm.org/pr20550 failing on FreeBSD-11')
    @expectedFailureGcc('GCC emits incomplete debug info.')
    @add_test_categories(['pyapi'])
    def test_with_python_api(self):
        """Test Python APIs on file and class static variables."""
        self.build()
        exe = os.path.join(os.getcwd(), "a.out")

        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target, VALID_TARGET)

        breakpoint = target.BreakpointCreateByLocation("main.cpp", self.line)
        self.assertTrue(breakpoint, VALID_BREAKPOINT)

        # Now launch the process, and do not stop at entry point.
        process = target.LaunchSimple (None, None, self.get_process_working_directory())
        self.assertTrue(process, PROCESS_IS_VALID)

        # The stop reason of the thread should be breakpoint.
        thread = process.GetThreadAtIndex(0)
        if thread.GetStopReason() != lldb.eStopReasonBreakpoint:
            from lldbsuite.test.lldbutil import stop_reason_to_str
            self.fail(STOPPED_DUE_TO_BREAKPOINT_WITH_STOP_REASON_AS %
                      stop_reason_to_str(thread.GetStopReason()))

        # Get the SBValue of 'A::g_points' and 'g_points'.
        frame = thread.GetFrameAtIndex(0)

        # arguments =>     False
        # locals =>        False
        # statics =>       True
        # in_scope_only => False
        valList = frame.GetVariables(False, False, True, False)

        for val in valList:
            self.DebugSBValue(val)
            name = val.GetName()
            self.assertTrue(name in ['g_points', 'A::g_points'])
            if name == 'g_points':
                self.assertTrue(val.GetValueType() == lldb.eValueTypeVariableStatic)
                self.assertTrue(val.GetNumChildren() == 2)
            elif name == 'A::g_points':
                self.assertTrue(val.GetValueType() == lldb.eValueTypeVariableGlobal)
                self.assertTrue(val.GetNumChildren() == 2)
                child1 = val.GetChildAtIndex(1)
                self.DebugSBValue(child1)
                child1_x = child1.GetChildAtIndex(0)
                self.DebugSBValue(child1_x)
                self.assertTrue(child1_x.GetTypeName() == 'int' and
                                child1_x.GetValue() == '11')

        # SBFrame.FindValue() should also work.
        val = frame.FindValue("A::g_points", lldb.eValueTypeVariableGlobal)
        self.DebugSBValue(val)
        self.assertTrue(val.GetName() == 'A::g_points')

        # Also exercise the "parameter" and "local" scopes while we are at it.
        val = frame.FindValue("argc", lldb.eValueTypeVariableArgument)
        self.DebugSBValue(val)
        self.assertTrue(val.GetName() == 'argc')

        val = frame.FindValue("argv", lldb.eValueTypeVariableArgument)
        self.DebugSBValue(val)
        self.assertTrue(val.GetName() == 'argv')

        val = frame.FindValue("hello_world", lldb.eValueTypeVariableLocal)
        self.DebugSBValue(val)
        self.assertTrue(val.GetName() == 'hello_world')