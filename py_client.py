#!/usr/bin/env python3
import json
import threading
import time
import websocket
import sys
import signal

# Configuración del servidor
# Esta IP debe ser actualizada con la IP pública de tu nueva instancia EC2
SERVER_IP = input("Ingresa la IP del servidor (o presiona Enter para usar 'localhost'): ") or "localhost"
SERVER_PORT = 8080
SERVER_URL = f"ws://{SERVER_IP}:{SERVER_PORT}"

# Variable para controlar la ejecución
running = True
connected = False
username = ""

# Estado del cliente
STATE_CONNECTING = 0
STATE_REGISTERING = 1
STATE_CONNECTED = 2
client_state = STATE_CONNECTING

# Función para obtener timestamp
def get_timestamp():
    from datetime import datetime
    return datetime.now().strftime("%Y-%m-%dT%H:%M:%S")

# Función para enviar mensaje de registro
def send_register_message(ws):
    global client_state
    message = {
        "type": "register",
        "sender": username,
        "content": "",
        "timestamp": get_timestamp()
    }
    ws.send(json.dumps(message))
    print("Enviando solicitud de registro...")
    client_state = STATE_REGISTERING

# Función para enviar mensaje broadcast
def send_broadcast_message(ws, content):
    message = {
        "type": "broadcast",
        "sender": username,
        "content": content,
        "timestamp": get_timestamp()
    }
    ws.send(json.dumps(message))

# Función para enviar mensaje privado
def send_private_message(ws, target, content):
    message = {
        "type": "private",
        "sender": username,
        "target": target,
        "content": content,
        "timestamp": get_timestamp()
    }
    ws.send(json.dumps(message))

# Función para solicitar lista de usuarios
def request_user_list(ws):
    message = {
        "type": "list_users",
        "sender": username,
        "content": "",
        "timestamp": get_timestamp()
    }
    ws.send(json.dumps(message))
    print("Solicitando lista de usuarios...")

# Función para solicitar información de usuario
def request_user_info(ws, target):
    message = {
        "type": "user_info",
        "sender": username,
        "target": target,
        "content": "",
        "timestamp": get_timestamp()
    }
    ws.send(json.dumps(message))
    print(f"Solicitando información del usuario {target}...")

# Función para cambiar estado
def change_status(ws, new_status):
    message = {
        "type": "change_status",
        "sender": username,
        "content": new_status,
        "timestamp": get_timestamp()
    }
    ws.send(json.dumps(message))
    print(f"Cambiando estado a {new_status}...")

# Función para enviar mensaje de desconexión
def send_disconnect_message(ws):
    message = {
        "type": "disconnect",
        "sender": username,
        "content": "",
        "timestamp": get_timestamp()
    }
    ws.send(json.dumps(message))

# Callback cuando se recibe un mensaje
def on_message(ws, message):
    global client_state
    
    try:
        data = json.loads(message)
        msg_type = data.get("type", "")
        sender = data.get("sender", "")
        content = data.get("content", "")
        timestamp = data.get("timestamp", "")
        
        # Procesar según tipo de mensaje
        if msg_type == "register_success":
            print("\n[SERVIDOR] Registro exitoso")
            client_state = STATE_CONNECTED
            
            # Mostrar lista de usuarios conectados
            user_list = data.get("userList", [])
            if user_list:
                print("Usuarios conectados:")
                for user in user_list:
                    print(f"  - {user}")
        
        elif msg_type == "error":
            print(f"\n[ERROR] {content}")
        
        elif msg_type == "broadcast":
            print(f"\n[DIFUSIÓN] [{timestamp}] {sender}: {content}")
        
        elif msg_type == "private":
            print(f"\n[PRIVADO] [{timestamp}] {sender}: {content}")
        
        elif msg_type == "list_users_response":
            print("\nLista de usuarios:")
            users = data.get("content", [])
            if isinstance(users, list):
                for user in users:
                    print(f"  - {user}")
        
        elif msg_type == "user_info_response":
            target = data.get("target", "")
            print(f"\nInformación del usuario {target}:")
            info = content
            if isinstance(info, dict):
                ip = info.get("ip", "")
                status = info.get("status", "")
                if ip:
                    print(f"  - IP: {ip}")
                if status:
                    print(f"  - Estado: {status}")
        
        elif msg_type == "status_update":
            if isinstance(content, dict):
                user = content.get("user", "")
                status = content.get("status", "")
                if user and status:
                    print(f"\n[ACTUALIZACIÓN] El usuario {user} ahora está {status}")
        
        elif msg_type == "user_connected":
            print(f"\n[SERVIDOR] {content}")
        
        elif msg_type == "user_disconnected":
            print(f"\n[SERVIDOR] Usuario {content} desconectado")
    
    except json.JSONDecodeError:
        print(f"Error al decodificar el mensaje: {message}")
    except Exception as e:
        print(f"Error al procesar el mensaje: {str(e)}")

# Callback cuando se abre la conexión
def on_open(ws):
    global connected
    print(f"Conexión establecida con el servidor {SERVER_URL}")
    connected = True
    send_register_message(ws)

# Callback cuando se cierra la conexión
def on_close(ws, close_status_code, close_msg):
    global connected, running, client_state
    connected = False
    client_state = STATE_CONNECTING
    print("Conexión cerrada")
    
    if running:
        print("Intentando reconectar en 5 segundos...")
        time.sleep(5)

# Callback para errores
def on_error(ws, error):
    global connected, client_state
    connected = False
    client_state = STATE_CONNECTING
    print(f"Error de conexión: {str(error)}")

# Función principal de la interfaz de usuario
def user_interface(ws):
    global running
    
    while running:
        # Si no estamos conectados, esperar
        if client_state != STATE_CONNECTED:
            time.sleep(1)
            continue
        
        try:
            # Mostrar prompt
            sys.stdout.write("\n> ")
            sys.stdout.flush()
            
            # Leer entrada de usuario
            command = input()
            
            # Procesar comandos
            if command.startswith("/"):
                if command in ["/exit", "/quit"]:
                    # Salir del programa
                    if connected:
                        send_disconnect_message(ws)
                    running = False
                    break
                
                elif command == "/list":
                    # Listar usuarios
                    if connected:
                        request_user_list(ws)
                
                elif command.startswith("/info "):
                    # Obtener información de usuario
                    target = command[6:].strip()
                    if connected and target:
                        request_user_info(ws, target)
                    else:
                        print("Uso: /info <nombre_usuario>")
                
                elif command.startswith("/status "):
                    # Cambiar estado
                    new_status = command[8:].strip()
                    if connected and new_status:
                        if new_status in ["ACTIVO", "OCUPADO", "INACTIVO"]:
                            change_status(ws, new_status)
                        else:
                            print("Estado no válido. Debe ser ACTIVO, OCUPADO o INACTIVO")
                    else:
                        print("Uso: /status <ACTIVO|OCUPADO|INACTIVO>")
                
                elif command.startswith("/msg "):
                    # Mensaje privado
                    parts = command[5:].strip().split(" ", 1)
                    if len(parts) == 2 and connected:
                        target, message = parts
                        send_private_message(ws, target, message)
                    else:
                        print("Uso: /msg <nombre_usuario> <mensaje>")
                
                elif command == "/help":
                    # Mostrar ayuda
                    print("\nComandos disponibles:")
                    print("  /list                 - Listar usuarios conectados")
                    print("  /info <usuario>       - Obtener información de un usuario")
                    print("  /status <estado>      - Cambiar estado (ACTIVO, OCUPADO, INACTIVO)")
                    print("  /msg <usuario> <msg>  - Enviar mensaje privado")
                    print("  /exit o /quit         - Salir del chat")
                    print("  /help                 - Mostrar esta ayuda")
                    print("\nCualquier otro texto se enviará como mensaje de difusión a todos los usuarios.")
                
                else:
                    print("Comando desconocido. Usa /help para ver los comandos disponibles.")
            
            else:
                # Mensaje de broadcast
                if connected and command.strip():
                    send_broadcast_message(ws, command)
        
        except Exception as e:
            print(f"Error en la interfaz de usuario: {str(e)}")

# Manejador de señales
def signal_handler(sig, frame):
    global running
    print("\nSaliendo...")
    running = False
    sys.exit(0)

# Función principal
def main():
    global username, running
    
    # Configurar manejador de señales
    signal.signal(signal.SIGINT, signal_handler)
    
    # Solicitar nombre de usuario
    username = input("Ingresa tu nombre de usuario: ")
    if not username:
        print("El nombre de usuario no puede estar vacío")
        sys.exit(1)
    
    # Crear websocket
    ws = websocket.WebSocketApp(
        SERVER_URL,
        on_open=on_open,
        on_message=on_message,
        on_error=on_error,
        on_close=on_close
    )
    
    # Crear hilo para la interfaz de usuario
    ui_thread = threading.Thread(target=user_interface, args=(ws,))
    ui_thread.daemon = True
    ui_thread.start()
    
    # Iniciar websocket (esto bloquea)
    ws.run_forever()
    
    # Si llegamos aquí, es porque la conexión se cerró
    while running:
        try:
            print("Intentando reconectar...")
            ws = websocket.WebSocketApp(
                SERVER_URL,
                on_open=on_open,
                on_message=on_message,
                on_error=on_error,
                on_close=on_close
            )
            ws.run_forever()
            time.sleep(5)  # Esperar antes de intentar reconectar
        except KeyboardInterrupt:
            running = False
            break

if __name__ == "__main__":
    main()
