The following should be possible eventually:
 - Desktop icons on the /desktop with their respective icons and displaynames. Capable of execution on click or various other things on right click.
 - Taskbar with app icons for 'pinned' apps and active apps, as well as even window previews.
 - Audio and network choosers, like how Windows has it. (See the Balcony github! It had implemented those before)
 - An actual background image/gif/audio on the desktop.


I do not care how these are created, whether it be strictly through plugins or whether it is something we have to implement ourselves in C++. It is something we need.

Furthermore, when you are done, do you have any ideas on how to get this more extensible? So we woudln't have to reimplement more and more every time someone wanted a feature like an audio visualizer or a custom render or something.

Lastly, replace SQLite with cereal (should be a very small swap). I simply want something less huge for this.
