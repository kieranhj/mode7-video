REM Usage: rip_gif_frames <input file> <output short name/dir> <geometry WxH>
@echo off
md %2
md %2\frames
md %2\bin
md %2\delta
md %2\files
md %2\disks
md %2\test
convert -coalesce %1 %2\frames\%2-%%d.png
