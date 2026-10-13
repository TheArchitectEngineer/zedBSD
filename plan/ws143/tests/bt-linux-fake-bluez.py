#!/usr/bin/env python3
# ws143-p007: a fake org.bluez for the host test of the Linux Bluetooth backend
# (userland/desktop/libkeiland-backend-linux/bluetooth-linux.c), on a private bus.
#   python3 -I plan/ws143/tests/bt-linux-fake-bluez.py BUS_ADDRESS
# It owns org.bluez with one adapter (/org/bluez/hci0) and the devices the test asks for, answers
# the backend's calls as BlueZ 5.82's documents say, calls the backend's agent during Pair as a
# script says, and is driven by the test through org.keiland.Test.Control(s command, s argument) -> s
# on /test (bus name org.keiland.TestFake).  Only the public D-Bus API documents of BlueZ were used, no BlueZ source.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import sys

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

ADAPTER_PATH = "/org/bluez/hci0"
AGENT_PATH = "/org/keiland/bluetooth/agent"
PROPS = "org.freedesktop.DBus.Properties"
OM = "org.freedesktop.DBus.ObjectManager"


class Failure(dbus.DBusException):
    def __init__(self, name, text=""):
        dbus.DBusException.__init__(self, text)
        self._dbus_error_name = name


class Fake:
    def __init__(self, bus):
        self.bus = bus
        self.calls = []
        self.devices = {}
        self.agent_owner = ""
        self.pair_script = "confirm"
        self.connect_error = ""
        self.agent_result = "none"
        self.pending_pair = None
        self.name = None


def emit_properties_changed(obj, interface, changed, invalidated):
    obj.PropertiesChanged(interface, dbus.Dictionary(changed, signature="sv"), dbus.Array(invalidated, signature="s"))


class Root(dbus.service.Object):
    """The root object: the ObjectManager signals come from here."""

    @dbus.service.signal(OM, signature="oa{sa{sv}}")
    def InterfacesAdded(self, path, interfaces):
        pass

    @dbus.service.signal(OM, signature="oas")
    def InterfacesRemoved(self, path, interfaces):
        pass


class Bluez(dbus.service.Object):
    """/org/bluez: the agent manager."""

    def __init__(self, fake):
        dbus.service.Object.__init__(self, fake.bus, "/org/bluez")
        self.fake = fake

    @dbus.service.method("org.bluez.AgentManager1", in_signature="os", out_signature="", sender_keyword="sender")
    def RegisterAgent(self, path, capability, sender=None):
        self.fake.calls.append("RegisterAgent " + str(capability))
        if self.fake.agent_owner == sender:
            raise Failure("org.bluez.Error.AlreadyExists")
        self.fake.agent_owner = sender

    @dbus.service.method("org.bluez.AgentManager1", in_signature="o", out_signature="", sender_keyword="sender")
    def RequestDefaultAgent(self, path, sender=None):
        self.fake.calls.append("RequestDefaultAgent")
        if self.fake.agent_owner != sender:
            raise Failure("org.bluez.Error.DoesNotExist")

    @dbus.service.method("org.bluez.AgentManager1", in_signature="o", out_signature="", sender_keyword="sender")
    def UnregisterAgent(self, path, sender=None):
        self.fake.calls.append("UnregisterAgent")
        self.fake.agent_owner = ""


class Adapter(dbus.service.Object):
    def __init__(self, fake):
        dbus.service.Object.__init__(self, fake.bus, ADAPTER_PATH)
        self.fake = fake
        self.props = {
            "Address": dbus.String("00:11:22:33:44:55"),
            "Alias": dbus.String("fake-hci0"),
            "Name": dbus.String("fake-hci0"),
            "Powered": dbus.Boolean(True),
            "Discovering": dbus.Boolean(False),
            "Class": dbus.UInt32(0x6c010c),
        }

    @dbus.service.method(PROPS, in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface):
        if interface != "org.bluez.Adapter1":
            raise Failure("org.freedesktop.DBus.Error.InvalidArgs")
        return dbus.Dictionary(self.props, signature="sv")

    @dbus.service.method(PROPS, in_signature="ssv", out_signature="")
    def Set(self, interface, name, value):
        self.fake.calls.append("Adapter.Set %s %s" % (name, int(value)))
        self.props[name] = dbus.Boolean(bool(value))
        emit_properties_changed(self, "org.bluez.Adapter1", {name: self.props[name]}, [])
        if name == "Powered" and not value and self.props["Discovering"]:
            self.props["Discovering"] = dbus.Boolean(False)
            emit_properties_changed(self, "org.bluez.Adapter1", {"Discovering": dbus.Boolean(False)}, [])

    @dbus.service.signal(PROPS, signature="sa{sv}as")
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    @dbus.service.method("org.bluez.Adapter1", in_signature="", out_signature="")
    def StartDiscovery(self):
        self.fake.calls.append("StartDiscovery")
        if not self.props["Powered"]:
            raise Failure("org.bluez.Error.NotReady")
        self.props["Discovering"] = dbus.Boolean(True)
        emit_properties_changed(self, "org.bluez.Adapter1", {"Discovering": dbus.Boolean(True)}, [])

    @dbus.service.method("org.bluez.Adapter1", in_signature="", out_signature="")
    def StopDiscovery(self):
        self.fake.calls.append("StopDiscovery")
        self.props["Discovering"] = dbus.Boolean(False)
        emit_properties_changed(self, "org.bluez.Adapter1", {"Discovering": dbus.Boolean(False)}, [])

    @dbus.service.method("org.bluez.Adapter1", in_signature="o", out_signature="")
    def RemoveDevice(self, path):
        self.fake.calls.append("RemoveDevice")
        for address, device in list(self.fake.devices.items()):
            if device.path == path:
                remove_device(self.fake, address)
                return
        raise Failure("org.bluez.Error.DoesNotExist")


class Device(dbus.service.Object):
    def __init__(self, fake, address, props, battery):
        self.path = ADAPTER_PATH + "/dev_" + address.replace(":", "_")
        dbus.service.Object.__init__(self, fake.bus, self.path)
        self.fake = fake
        self.address = address
        self.props = props
        self.battery = battery

    def interfaces(self):
        result = {"org.bluez.Device1": dbus.Dictionary(self.props, signature="sv")}
        if self.battery is not None:
            result["org.bluez.Battery1"] = dbus.Dictionary({"Percentage": dbus.Byte(self.battery)}, signature="sv")
        return dbus.Dictionary(result, signature="sa{sv}")

    def set(self, name, value):
        self.props[name] = value
        emit_properties_changed(self, "org.bluez.Device1", {name: value}, [])

    @dbus.service.method(PROPS, in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface):
        if interface == "org.bluez.Device1":
            return dbus.Dictionary(self.props, signature="sv")
        if interface == "org.bluez.Battery1" and self.battery is not None:
            return dbus.Dictionary({"Percentage": dbus.Byte(self.battery)}, signature="sv")
        raise Failure("org.freedesktop.DBus.Error.InvalidArgs", "No such interface")

    @dbus.service.method(PROPS, in_signature="ssv", out_signature="")
    def Set(self, interface, name, value):
        self.fake.calls.append("Device.Set %s %s %s" % (self.address, name, int(value)))
        self.set(name, dbus.Boolean(bool(value)))

    @dbus.service.signal(PROPS, signature="sa{sv}as")
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    @dbus.service.method("org.bluez.Device1", in_signature="", out_signature="", async_callbacks=("ok", "err"))
    def Pair(self, ok, err):
        fake = self.fake
        fake.calls.append("Pair " + self.address)
        script = fake.pair_script
        if script.startswith("fail:"):
            parts = script.split(":", 2)
            err(Failure(parts[1], parts[2] if len(parts) > 2 else ""))
            return
        if script == "hang":
            fake.pending_pair = (self, ok, err)
            return
        agent = agent_interface(fake)
        if agent is None:
            err(Failure("org.bluez.Error.AuthenticationFailed"))
            return

        def paired():
            self.set("Paired", dbus.Boolean(True))
            self.set("Bonded", dbus.Boolean(True))
            ok()

        def refused(error):
            fake.agent_result = error.get_dbus_name()
            err(Failure("org.bluez.Error.AuthenticationFailed" if script == "pin" else "org.bluez.Error.AuthenticationRejected"))

        if script == "confirm":
            agent.RequestConfirmation(dbus.ObjectPath(self.path), dbus.UInt32(123456), reply_handler=paired, error_handler=refused, timeout=25)
        elif script == "confirm-cancel":
            fake.pending_pair = (self, ok, err)
            agent.RequestConfirmation(dbus.ObjectPath(self.path), dbus.UInt32(111111), reply_handler=lambda: None, error_handler=lambda e: None, timeout=25)

            def cancel():
                agent.Cancel(reply_handler=lambda: None, error_handler=lambda e: None)
                err(Failure("org.bluez.Error.AuthenticationCanceled"))
                fake.pending_pair = None
                return False
            GLib.timeout_add(300, cancel)
        elif script == "pin":
            agent.RequestPinCode(dbus.ObjectPath(self.path), reply_handler=lambda pin: paired(), error_handler=refused, timeout=25)
        elif script == "passkey":
            agent.DisplayPasskey(dbus.ObjectPath(self.path), dbus.UInt32(654321), dbus.UInt16(0), reply_handler=lambda: None, error_handler=lambda e: None, timeout=25)

            def later():
                paired()
                return False
            GLib.timeout_add(500, later)
        else:
            err(Failure("org.bluez.Error.Failed", "unknown script"))

    @dbus.service.method("org.bluez.Device1", in_signature="", out_signature="")
    def CancelPairing(self):
        self.fake.calls.append("CancelPairing " + self.address)
        pending = self.fake.pending_pair
        if pending is not None and pending[0] is self:
            self.fake.pending_pair = None
            pending[2](Failure("org.bluez.Error.AuthenticationCanceled"))

    @dbus.service.method("org.bluez.Device1", in_signature="", out_signature="")
    def Connect(self):
        self.fake.calls.append("Connect " + self.address)
        if self.fake.connect_error:
            raise Failure(self.fake.connect_error)
        self.set("Connected", dbus.Boolean(True))

    @dbus.service.method("org.bluez.Device1", in_signature="", out_signature="")
    def Disconnect(self):
        self.fake.calls.append("Disconnect " + self.address)
        if not self.props.get("Connected", False):
            raise Failure("org.bluez.Error.NotConnected")
        self.set("Connected", dbus.Boolean(False))


def agent_interface(fake):
    if not fake.agent_owner:
        return None
    proxy = fake.bus.get_object(fake.agent_owner, AGENT_PATH, introspect=False)
    return dbus.Interface(proxy, "org.bluez.Agent1")


def parse(argument):
    result = {}
    for item in argument.split(";"):
        if "=" in item:
            key, value = item.split("=", 1)
            result[key] = value
    return result


def add_device(fake, spec):
    address = spec["address"]
    props = {
        "Address": dbus.String(address),
        "AddressType": dbus.String("random" if spec.get("kind") == "le-random" else "public"),
        "Alias": dbus.String(spec.get("name") or address.replace(":", "-")),
        "Paired": dbus.Boolean(spec.get("paired") == "1"),
        "Bonded": dbus.Boolean(spec.get("paired") == "1"),
        "Trusted": dbus.Boolean(spec.get("trusted") == "1"),
        "Connected": dbus.Boolean(False),
        "LegacyPairing": dbus.Boolean(False),
        "Blocked": dbus.Boolean(False),
        "UUIDs": dbus.Array(["00001124-0000-1000-8000-00805f9b34fb"] * 6, signature="s"),
    }
    if spec.get("name") == "@huge":
        spec["name"] = "H" * 70000
        props["Alias"] = dbus.String(spec["name"])
    if spec.get("name"):
        props["Name"] = dbus.String(spec["name"])
    if spec.get("icon"):
        props["Icon"] = dbus.String(spec["icon"])
    if spec.get("kind", "bredr") == "bredr":
        props["Class"] = dbus.UInt32(0x2540)
    else:
        props["Appearance"] = dbus.UInt16(0x3c1)
    if spec.get("rssi"):
        props["RSSI"] = dbus.Int16(int(spec["rssi"]))
    battery = None
    if spec.get("battery"):
        battery = int(spec["battery"])
    device = Device(fake, address, props, battery)
    fake.devices[address] = device
    if spec.get("announce", "1") == "1":
        fake.root.InterfacesAdded(dbus.ObjectPath(device.path), device.interfaces())


def remove_device(fake, address):
    device = fake.devices.pop(address)
    interfaces = ["org.bluez.Device1"]
    if device.battery is not None:
        interfaces.append("org.bluez.Battery1")
    fake.root.InterfacesRemoved(dbus.ObjectPath(device.path), dbus.Array(interfaces, signature="s"))
    device.remove_from_connection()


class Test(dbus.service.Object):
    def __init__(self, fake):
        dbus.service.Object.__init__(self, fake.bus, "/test")
        self.fake = fake

    @dbus.service.method("org.keiland.Test", in_signature="ss", out_signature="s")
    def Control(self, command, argument):
        fake = self.fake
        spec = parse(argument)
        if command == "add":
            add_device(fake, spec)
        elif command == "set":
            device = fake.devices[spec["address"]]
            device.set(spec["name"], dbus.Boolean(spec["value"] == "1"))
        elif command == "rssi":
            fake.devices[spec["address"]].set("RSSI", dbus.Int16(int(spec["value"])))
        elif command == "invalidate":
            device = fake.devices[spec["address"]]
            device.props.pop("RSSI", None)
            emit_properties_changed(device, "org.bluez.Device1", {}, ["RSSI"])
        elif command == "remove":
            remove_device(fake, spec["address"])
        elif command == "script":
            fake.pair_script = argument
        elif command == "connect-error":
            fake.connect_error = argument
        elif command == "query":
            device = fake.devices[spec["address"]]
            return "paired=%d trusted=%d connected=%d" % (int(device.props["Paired"]), int(device.props["Trusted"]), int(device.props["Connected"]))
        elif command == "calls":
            return "\n".join(fake.calls)
        elif command == "agent-owner":
            return fake.agent_owner
        elif command == "agent-result":
            return fake.agent_result
        elif command == "trigger":
            trigger(fake, spec)
        elif command == "release-name":
            fake.bus.release_name("org.bluez")
            fake.agent_owner = ""
        elif command == "acquire-name":
            fake.bus.request_name("org.bluez")
        else:
            raise Failure("org.keiland.Test.Error", "unknown command " + command)
        return "ok"


def trigger(fake, spec):
    """Calls the agent as BlueZ would outside a Pair; the answer is kept for agent-result."""
    fake.agent_result = "pending"
    agent = agent_interface(fake)
    path = ""
    if "address" in spec:
        path = dbus.ObjectPath(fake.devices[spec["address"]].path)

    def done(*values):
        fake.agent_result = "ok"

    def failed(error):
        fake.agent_result = error.get_dbus_name()

    what = spec["what"]
    if what == "authorize":
        agent.AuthorizeService(path, dbus.String(spec["uuid"]), reply_handler=done, error_handler=failed, timeout=25)
    elif what == "authorization":
        agent.RequestAuthorization(path, reply_handler=done, error_handler=failed, timeout=25)
    elif what == "passkey-request":
        agent.RequestPasskey(path, reply_handler=done, error_handler=failed, timeout=25)
    elif what == "release":
        agent.Release(reply_handler=done, error_handler=failed, timeout=25)
    elif what == "cancel":
        agent.Cancel(reply_handler=done, error_handler=failed, timeout=25)


def main():
    DBusGMainLoop(set_as_default=True)
    bus = dbus.bus.BusConnection(sys.argv[1])
    fake = Fake(bus)
    fake.root = Root(bus, "/")
    fake.bluez = Bluez(fake)
    fake.adapter = Adapter(fake)
    fake.test = Test(fake)
    add_device(fake, parse("address=AA:00:00:00:00:01;name=Keyboard K;icon=input-keyboard;paired=1;trusted=0;battery=80;kind=bredr;announce=0"))
    add_device(fake, parse("address=AA:00:00:00:00:02;name=Mouse M;icon=input-mouse;paired=0;rssi=-50;kind=bredr;announce=0"))
    add_device(fake, parse("address=CC:00:00:00:00:03;name=Laser L;icon=input-mouse;paired=0;rssi=-70;kind=le-random;announce=0"))
    fake.control_name = dbus.service.BusName("org.keiland.TestFake", bus)
    fake.name = dbus.service.BusName("org.bluez", bus)
    print("fake-bluez: ready", flush=True)
    GLib.MainLoop().run()


main()
