"""
Test whether a process started by lldb has no extra file descriptors open.
"""

from __future__ import print_function



import os
import lldb
from lldbsuite.test.lldbtest import *
import lldbsuite.test.lldbutil as lldbutil


def python_leaky_fd_version(test):
    import sys
    # Python random module leaks file descriptors on some versions.
    return sys.version_info >= (2, 7, 8) and sys.version_info < (2, 7, 10)


class AvoidsFdLeakTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @expectedFailure(python_leaky_fd_version, "bugs.freebsd.org/197376")
    @skipIfWindows # The check for descriptor leakage needs to be implemented differently here.
    @skipIfTargetAndroid() # Android have some other file descriptors open by the shell
    def test_fd_leak_basic (self):
        self.do_test([])

    @expectedFailure(python_leaky_fd_version, "bugs.freebsd.org/197376")
    @skipIfWindows # The check for descriptor leakage needs to be implemented differently here.
    @skipIfTargetAndroid() # Android have some other file descriptors open by the shell
    def test_fd_leak_log (self):
        self.do_test(["log enable -f '/dev/null' lldb commands"])

    def do_test (self, commands):
        self.build()
        exe = os.path.join (os.getcwd(), "a.out")

        for c in commands:
            self.runCmd(c)

        target = self.dbg.CreateTarget(exe)

        process = target.LaunchSimple (None, None, self.get_process_working_directory())
        self.assertTrue(process, PROCESS_IS_VALID)

        self.assertTrue(process.GetState() == lldb.eStateExited, "Process should have exited.")
        self.assertTrue(process.GetExitStatus() == 0,
                "Process returned non-zero status. Were incorrect file descriptors passed?")

    @expectedFailure(python_leaky_fd_version, "bugs.freebsd.org/197376")
    @expectedFlakeyLinux
    @skipIfWindows # The check for descriptor leakage needs to be implemented differently here.
    @skipIfTargetAndroid() # Android have some other file descriptors open by the shell
    def test_fd_leak_multitarget (self):
        self.build()
        exe = os.path.join (os.getcwd(), "a.out")

        target = self.dbg.CreateTarget(exe)
        breakpoint = target.BreakpointCreateBySourceRegex ('Set breakpoint here', lldb.SBFileSpec ("main.c", False))
        self.assertTrue(breakpoint, VALID_BREAKPOINT)

        process1 = target.LaunchSimple (None, None, self.get_process_working_directory())
        self.assertTrue(process1, PROCESS_IS_VALID)
        self.assertTrue(process1.GetState() == lldb.eStateStopped, "Process should have been stopped.")

        target2 = self.dbg.CreateTarget(exe)
        process2 = target2.LaunchSimple (None, None, self.get_process_working_directory())
        self.assertTrue(process2, PROCESS_IS_VALID)

        self.assertTrue(process2.GetState() == lldb.eStateExited, "Process should have exited.")
        self.assertTrue(process2.GetExitStatus() == 0,
                "Process returned non-zero status. Were incorrect file descriptors passed?")
