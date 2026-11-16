# RevF is provisioned through a 1.8 V five-pad SWD fixture (FT4232H/OpenOCD
# locally); J-Link also works when available.
board_runner_args(jlink "--device=nRF54L15_M33" "--speed=1000")
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
