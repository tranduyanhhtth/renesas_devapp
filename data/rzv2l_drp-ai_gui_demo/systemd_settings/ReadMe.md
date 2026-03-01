# RZV2L Demo System Setup
These scripts configure Systemd for auto start and Wayland LCD display

---
## Wayland Settings

Copy the wayland configuration file weston.ini to the following directory
```
/etc/xdg/weston/weston.ini
```

Restart Weston
```
systemctl restart weston@root
```

---
## Auto Start application with systemd
Copy the service file "autorun.service" to ***/etc/systemd/system/***
Set the service permissions

```
sudo chmod 640
```

Restart systemd
```
systemctl --system daemon-reload
```

Enable autorun service
```
systemctl enable autorun
```
The below steps must match the line in the service file that calls the bash script
```
ExecStart=/home/root/Scripts/autostart_app_launch.sh
```

Create a directory named Script in the root home directory.
```
mkdir /home/root/Script
```

Copy the bash file ***autostart_app_launch.sh*** to the Created directory and set permission to executable.

```
chmod +x Scripts/autostart_app_launch.sh
```

Create the directory *** /home/root/RZV_AI_Demo***. 

```
mkdir /home/root/RZV_AI_Demo
```
Copy the contents of the exe directory in you cloned repo to this new directory


Start the autorun.service
```
systemctl start autorun.service
```

Restart the board.



