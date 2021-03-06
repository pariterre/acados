#
# Copyright 2019 Gianluca Frison, Dimitris Kouzoupis, Robin Verschueren, Andrea Zanelli, Niels van Duijkeren, Jonathan Frey, Tommaso Sartor, Branimir Novoselnik, Rien Quirynen, Rezart Qelibari, Dang Doan, Jonas Koenemann, Yutao Chen, Tobias Schöls, Jonas Schlagenhauf, Moritz Diehl
#
# This file is part of acados.
#
# The 2-Clause BSD License
#
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

find_package(Matlab COMPONENTS MAIN_PROGRAM REQUIRED)

add_test(NAME ocp_qp_matlab
    # run() is not useful here as it changes cwd, so we have to use addpath()
    COMMAND ${Matlab_MAIN_PROGRAM} "-noFigureWindows" "-nodesktop" "-nosplash" "-nosoftwareopengl" "-r"
        "addpath('${CMAKE_CURRENT_SOURCE_DIR}'); try, ocp_qp_example, catch ME, disp(ME.getReport()), exit(1), end, exit(0)")

# add_test(NAME ocp_nlp_matlab
#     COMMAND ${Matlab_MAIN_PROGRAM} "-noFigureWindows" "-nodesktop" "-nosplash" "-nosoftwareopengl" "-r"
#         "addpath('${CMAKE_CURRENT_SOURCE_DIR}'); try, ocp_nlp_example, catch ME, disp(ME.getReport()), exit(1), end, exit(0)")

# add_test(NAME sim_matlab
    # COMMAND ${Matlab_MAIN_PROGRAM} "-noFigureWindows" "-nodesktop" "-nosplash" "-nosoftwareopengl" "-r"
        # "addpath('${CMAKE_CURRENT_SOURCE_DIR}'); try, sim_example, catch ME, disp(ME.getReport()), exit(1), end, exit(0)")

set_tests_properties(ocp_qp_matlab
    PROPERTIES
        ENVIRONMENT "MATLABPATH=${CMAKE_BINARY_DIR}/swig/matlab/:$ENV{MATLABPATH}")
