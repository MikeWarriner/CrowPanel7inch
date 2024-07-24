# CrowPanel7inch
I've found the CrowPanel examples to be terrible, they don't work, have missing files and are badly written.

So here are some templates that I've written to get the CrowPanel 7" HMI display to work (ESP32).  If you find them useful great, if they don't work please let me know and I'll work to make this easier and easier to use.

To get started:

1. Install Visual Studio Code
2. Install GOLANG (you need it for PlatformIO)
3. Install the PlatformIO extension
4. Install the Espressif board definitions
  a/ Click on the PlatformIO icon in Visual Studio
  b/ Click on Platforms
  c/ Add the Espressif board definitions
5. Install the board definition for the CrowPanel 7"
  a/ From a command prompt, go into the BoardDefinitions directory
  c/ Run install_win.bat or install_mac.sh to install the definitions
6. Load the project Startup by opening that folder in Visual Studio Code
7. Build the code by clicking on the PlatformIO icon and clicking BUILD, or by clicking the TICK in the status bar
8. Deploy it by clicking on PlatformIO icon and "Upload and Monitor" or the -> arrow in the status bar.  Note that this compiles it so you don't need to recompile and redeploy separately next time.

Note down any errors in the process and fix them as you go.  If you find steps missing or broken please let me know!



