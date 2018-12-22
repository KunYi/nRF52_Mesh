import paho.mqtt.client as mqtt
from ouimeaux.environment import Environment
#https://ouimeaux.readthedocs.io/en/latest/
import datetime
import logging as log
import cfg
from time import sleep
import socket
import json

# -------------------- mqtt events -------------------- 
def on_connect(lclient, userdata, flags, rc):
    log.info("mqtt connected with result code "+str(rc))
    lclient.subscribe("zig/tree")

def on_zig_tree(payload):
    log.info("MQTT> tree button press")
    if("Wemo Bed Power" in devices):
        sensor = json.loads(payload)
        if("click" in sensor and sensor["click"] == "single"):
            #state = devices["Wemo Bed Power"].basicevent.GetBinaryState()
            #the state is inconsistent and does not get updated soon enough ~ 10 sec
            log.info("Wemo Tree> switching on")
            devices["Wemo Bed Power"].on()
        elif("action" in sensor and sensor["action"] == "hold"):
                log.info("Wemo Tree> switching off")
                devices["Wemo Bed Power"].off()
        else:
            log.error("Error> unhandled button event")
    else:
        log.error("Error> No 'Wemo Bed Power' available ")
    return

def on_message(client, userdata, msg):
    topic_parts = msg.topic.split('/')
    log.info("MQTT> message %s",msg.payload)
    try:
        if( (len(topic_parts) == 2) and (topic_parts[1] == "tree") ):
            on_zig_tree(msg.payload)
        elif( (len(topic_parts) == 3) and (topic_parts[0] == "Nodes") ):
            nodeid = topic_parts[1]
            sensor = topic_parts[2]
            measurement = "node"+nodeid
            value = float(str(msg.payload))
            post = [
                {
                    "measurement": measurement,
                    "time": datetime.datetime.utcnow(),
                    "fields": {
                        sensor: value
                    }
                }
            ]
            #TODO use post
            log.debug(msg.topic+" "+str(msg.payload)+" posted")
    except ValueError:
        log.error(" ValueError with : "+msg.topic+" "+str(msg.payload))


# -------------------- wemo events -------------------- 
def on_switch(switch):
    log.info("Switch found! %s", switch.name)
    config["devices"][switch.name]["found"] = True
def on_motion(motion):
    log.info("Motion found! %s", motion.name)

def wemo_start():
    for device_name in config["devices"]:
        config["devices"][device_name]["found"] = False
    env = Environment(on_switch,on_motion)
    env.start()
    env.discover(seconds=3)
    #print("listing:")
    #env.list_switches()
    devices = {}
    for device_name in config["devices"]:
        if(config["devices"][device_name]["found"]):
            switch = env.get_switch(device_name)
            #switch["node"] = config["devices"][device_name]["node"] # no support for assignment
            devices[device_name] = switch
    #print("explain()")
    #switch.explain()
    return devices


def mqtt_publish_loop():
    for name in devices:
        log.debug("%s: bin state: %s",name,devices[name].basicevent.GetBinaryState())
        topic = "Nodes/"+str(config["devices"][name]["node"])+"/power"
        power = float(devices[name].current_power)/1000
        clientMQTT.publish(topic,power)
        log.debug("%s: %s: %s",name, topic, power)
    return

def wemo_loop_forever():
    while(True):
        clientMQTT.loop()
        #sleep(0.01)
        mqtt_publish_loop()
        sleep(config["poll_sec"])
    return

def mqtt_connect_retries(client):
    connected = False
    while(not connected):
        try:
            client.connect(config["mqtt"]["host"], config["mqtt"]["port"], config["mqtt"]["keepalive"])
            connected = True
        except socket.error:
            log.error("socket.error will try a reconnection in 10 s")
        sleep(10)
    return

def mqtt_start():
    cid = config["mqtt"]["client_id"] +"_"+socket.gethostname()
    clientMQTT = mqtt.Client(client_id=cid)
    clientMQTT.on_connect = on_connect
    clientMQTT.on_message = on_message
    mqtt_connect_retries(clientMQTT)
    clientMQTT.loop_start()
    return clientMQTT

# -------------------- main -------------------- 
config = cfg.configure_log(__file__)

#will start a separate thread for looping
clientMQTT = mqtt_start()
log.info("=> mqtt started")

devices = wemo_start()
log.info("=> wemo started")

#loop forever
wemo_loop_forever()
