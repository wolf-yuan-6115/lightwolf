export const PUMPER_UDEV_RULE =
  'ACTION!="remove", SUBSYSTEM=="hidraw", KERNEL=="hidraw*", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="f10a", MODE:="0660", TAG+="uaccess"';

export const PUMPER_PERSISTENT_UDEV_COMMAND = `sudo install -d -m 0755 /etc/udev/rules.d
printf '%s\\n' '${PUMPER_UDEV_RULE}' | sudo tee /etc/udev/rules.d/70-pumper-webhid.rules >/dev/null
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw --action=add`;

export const PUMPER_TEMPORARY_UDEV_COMMAND = `sudo install -d -m 0755 /run/udev/rules.d
printf '%s\\n' '${PUMPER_UDEV_RULE}' | sudo tee /run/udev/rules.d/70-pumper-webhid.rules >/dev/null
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw --action=add`;
