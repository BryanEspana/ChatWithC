#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include "cJSON.h"

#define MAX_BUFFER 2048
#define RECONNECT_DELAY 5
#define MAX_RECONNECT_ATTEMPTS 5

// Estados del cliente
#define STATE_CONNECTING 0
#define STATE_REGISTERING 1
#define STATE_CONNECTED 2

// Información del servidor
char *server_ip = NULL;
int server_port = 8080;
int reconnect_attempts = 0;

// Nombre de usuario
char username[50];

// Estado del cliente
int client_state = STATE_CONNECTING;

// Socket para el cliente
int client_socket = -1;

// Flags para control
volatile int is_running = 1;
volatile int force_reconnect = 0;

// Función para generar timestamp
void get_timestamp(char *timestamp, size_t size) {
    time_t now;
    struct tm *timeinfo;
    
    time(&now);
    timeinfo = localtime(&now);
    
    strftime(timestamp, size, "%Y-%m-%dT%H:%M:%S", timeinfo);
}

// Función para enviar mensaje al servidor
int send_message(const char *message) {
    if (client_socket < 0) {
        printf("Error: No conectado al servidor\n");
        return -1;
    }
    
    // Enviar el mensaje
    int bytes_sent = send(client_socket, message, strlen(message), 0);
    if (bytes_sent < 0) {
        perror("Error al enviar mensaje");
        return -1;
    }
    
    return bytes_sent;
}

// Enviar mensaje de registro al servidor
void send_register_message() {
    // Crear mensaje JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "register");
    cJSON_AddStringToObject(root, "sender", username);
    cJSON_AddStringToObject(root, "content", "");
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);
    
    // Convertir a string
    char *json_str = cJSON_Print(root);
    
    // Enviar mensaje
    printf("Enviando solicitud de registro...\n");
    if (send_message(json_str) > 0) {
        client_state = STATE_REGISTERING;
    } else {
        printf("Error al enviar mensaje de registro\n");
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Enviar mensaje de broadcast
void send_broadcast_message(const char *message) {
    // Crear mensaje JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "broadcast");
    cJSON_AddStringToObject(root, "sender", username);
    cJSON_AddStringToObject(root, "content", message);
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);
    
    // Convertir a string
    char *json_str = cJSON_Print(root);
    
    // Enviar mensaje
    if (send_message(json_str) <= 0) {
        printf("Error al enviar mensaje\n");
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Enviar mensaje privado
void send_private_message(const char *target, const char *message) {
    // Crear mensaje JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "private");
    cJSON_AddStringToObject(root, "sender", username);
    cJSON_AddStringToObject(root, "target", target);
    cJSON_AddStringToObject(root, "content", message);
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);
    
    // Convertir a string
    char *json_str = cJSON_Print(root);
    
    // Enviar mensaje
    if (send_message(json_str) <= 0) {
        printf("Error al enviar mensaje privado\n");
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Solicitar lista de usuarios
void request_user_list() {
    // Crear mensaje JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "list_users");
    cJSON_AddStringToObject(root, "sender", username);
    cJSON_AddStringToObject(root, "content", "");
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);
    
    // Convertir a string
    char *json_str = cJSON_Print(root);
    
    // Enviar mensaje
    if (send_message(json_str) <= 0) {
        printf("Error al solicitar lista de usuarios\n");
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Solicitar información de un usuario
void request_user_info(const char *target) {
    // Crear mensaje JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "get_user_info");
    cJSON_AddStringToObject(root, "sender", username);
    cJSON_AddStringToObject(root, "target", target);
    cJSON_AddStringToObject(root, "content", "");
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);
    
    // Convertir a string
    char *json_str = cJSON_Print(root);
    
    // Enviar mensaje
    if (send_message(json_str) <= 0) {
        printf("Error al solicitar información de usuario\n");
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Cambiar estado
void change_status(const char *new_status) {
    // Crear mensaje JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "change_status");
    cJSON_AddStringToObject(root, "sender", username);
    cJSON_AddStringToObject(root, "content", new_status);
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);
    
    // Convertir a string
    char *json_str = cJSON_Print(root);
    
    // Enviar mensaje
    if (send_message(json_str) <= 0) {
        printf("Error al cambiar estado\n");
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Enviar mensaje de desconexión
void send_disconnect_message() {
    // Crear mensaje JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "disconnect");
    cJSON_AddStringToObject(root, "sender", username);
    cJSON_AddStringToObject(root, "content", "");
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);
    
    // Convertir a string
    char *json_str = cJSON_Print(root);
    
    // Enviar mensaje
    if (send_message(json_str) <= 0) {
        printf("Error al enviar mensaje de desconexión\n");
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Procesar mensaje recibido
void process_message(const char *message, size_t len) {
    // Parsear JSON
    cJSON *json = cJSON_Parse(message);
    if (!json) {
        printf("Error al parsear mensaje JSON: %s\n", message);
        return;
    }
    
    // Obtener tipo de mensaje
    cJSON *type_obj = cJSON_GetObjectItem(json, "type");
    if (!type_obj || !cJSON_IsString(type_obj)) {
        printf("Error: Mensaje sin campo 'type'\n");
        cJSON_Delete(json);
        return;
    }
    
    char *type = type_obj->valuestring;
    
    // Obtener remitente
    cJSON *sender_obj = cJSON_GetObjectItem(json, "sender");
    char *sender = "";
    if (sender_obj && cJSON_IsString(sender_obj)) {
        sender = sender_obj->valuestring;
    }
    
    // Obtener contenido
    cJSON *content_obj = cJSON_GetObjectItem(json, "content");
    char *content = "";
    if (content_obj && cJSON_IsString(content_obj)) {
        content = content_obj->valuestring;
    }
    
    // Procesar según tipo
    if (strcmp(type, "register_response") == 0) {
        // Respuesta a registro
        if (strcmp(content, "success") == 0) {
            printf("\nRegistro exitoso. ¡Bienvenido al chat!\n");
            client_state = STATE_CONNECTED;
        } else {
            printf("\nError en el registro: %s\n", content);
            is_running = 0;
        }
    } else if (strcmp(type, "broadcast") == 0) {
        // Mensaje de difusión
        if (strcmp(sender, username) != 0) {
            printf("\n[%s]: %s\n", sender, content);
        }
    } else if (strcmp(type, "private") == 0) {
        // Mensaje privado
        cJSON *target_obj = cJSON_GetObjectItem(json, "target");
        if (target_obj && cJSON_IsString(target_obj)) {
            char *target = target_obj->valuestring;
            
            if (strcmp(target, username) == 0) {
                // Mensaje para nosotros
                printf("\n[DM de %s]: %s\n", sender, content);
            } else if (strcmp(sender, username) == 0) {
                // Mensaje enviado por nosotros
                printf("\n[DM para %s]: %s\n", target, content);
            }
        }
    } else if (strcmp(type, "user_list") == 0) {
        // Lista de usuarios
        cJSON *users = cJSON_GetObjectItem(json, "users");
        if (users && cJSON_IsArray(users)) {
            int count = cJSON_GetArraySize(users);
            printf("\nUsuarios conectados (%d):\n", count);
            
            for (int i = 0; i < count; i++) {
                cJSON *user = cJSON_GetArrayItem(users, i);
                if (user && cJSON_IsString(user)) {
                    printf("  - %s\n", user->valuestring);
                }
            }
        }
    } else if (strcmp(type, "user_info") == 0) {
        // Información de usuario
        cJSON *target_obj = cJSON_GetObjectItem(json, "target");
        cJSON *status_obj = cJSON_GetObjectItem(json, "status");
        cJSON *connected_since_obj = cJSON_GetObjectItem(json, "connected_since");
        
        if (target_obj && cJSON_IsString(target_obj) &&
            status_obj && cJSON_IsString(status_obj) &&
            connected_since_obj && cJSON_IsString(connected_since_obj)) {
            
            printf("\nInformación de usuario:\n");
            printf("  Usuario: %s\n", target_obj->valuestring);
            printf("  Estado: %s\n", status_obj->valuestring);
            printf("  Conectado desde: %s\n", connected_since_obj->valuestring);
        }
    } else if (strcmp(type, "status_changed") == 0) {
        // Cambio de estado
        printf("\n[SERVIDOR] Usuario %s cambió su estado a %s\n", sender, content);
    } else if (strcmp(type, "user_connected") == 0) {
        // Usuario conectado
        printf("\n[SERVIDOR] Usuario %s conectado\n", content);
    } else if (strcmp(type, "user_disconnected") == 0) {
        // Usuario desconectado
        printf("\n[SERVIDOR] Usuario %s desconectado\n", content);
    }
    
    cJSON_Delete(json);
}

// Thread para recibir mensajes
void *receiver_thread(void *arg) {
    char buffer[MAX_BUFFER];
    
    while (is_running) {
        // Si no estamos conectados, esperar
        if (client_socket < 0 || client_state != STATE_CONNECTED) {
            sleep(1);
            continue;
        }
        
        // Leer datos
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_received > 0) {
            // Asegurar null-termination
            buffer[bytes_received] = '\0';
            
            // Procesar mensaje
            process_message(buffer, bytes_received);
        } else if (bytes_received == 0) {
            // Conexión cerrada por el servidor
            printf("\nConexión cerrada por el servidor\n");
            close(client_socket);
            client_socket = -1;
            client_state = STATE_CONNECTING;
            
            if (is_running) {
                printf("Intentando reconectar en %d segundos...\n", RECONNECT_DELAY);
                sleep(RECONNECT_DELAY);
            }
            
        } else {
            // Error
            if (is_running) {
                perror("Error al recibir datos");
                close(client_socket);
                client_socket = -1;
                client_state = STATE_CONNECTING;
                
                printf("Intentando reconectar en %d segundos...\n", RECONNECT_DELAY);
                sleep(RECONNECT_DELAY);
            }
        }
    }
    
    return NULL;
}

// Thread para la interfaz de usuario
void *user_interface_thread(void *arg) {
    char buffer[MAX_BUFFER];
    
    while (is_running) {
        // Si no estamos conectados, esperar
        if (client_state != STATE_CONNECTED) {
            sleep(1);
            continue;
        }
        
        // Mostrar prompt
        printf("\n> ");
        fflush(stdout);
        
        // Leer entrada de usuario
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }
        
        // Eliminar el salto de línea
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
            len--;
        }
        
        // Si es una línea vacía, continuar
        if (len == 0) {
            continue;
        }
        
        // Procesar comandos
        if (buffer[0] == '/') {
            // Comandos especiales
            if (strncmp(buffer, "/exit", 5) == 0 || 
                strncmp(buffer, "/quit", 5) == 0) {
                // Salir del programa
                send_disconnect_message();
                is_running = 0;
                break;
                
            } else if (strncmp(buffer, "/list", 5) == 0) {
                // Listar usuarios
                request_user_list();
                
            } else if (strncmp(buffer, "/info ", 6) == 0) {
                // Obtener información de usuario
                char *target = buffer + 6;
                if (strlen(target) > 0) {
                    request_user_info(target);
                } else {
                    printf("Uso: /info <nombre_usuario>\n");
                }
                
            } else if (strncmp(buffer, "/status ", 8) == 0) {
                // Cambiar estado
                char *new_status = buffer + 8;
                if (strlen(new_status) > 0) {
                    if (strcmp(new_status, "ACTIVO") == 0 || 
                        strcmp(new_status, "OCUPADO") == 0 || 
                        strcmp(new_status, "INACTIVO") == 0) {
                        change_status(new_status);
                    } else {
                        printf("Estado no válido. Debe ser ACTIVO, OCUPADO o INACTIVO\n");
                    }
                } else {
                    printf("Uso: /status <ACTIVO|OCUPADO|INACTIVO>\n");
                }
                
            } else if (strncmp(buffer, "/msg ", 5) == 0) {
                // Mensaje privado
                char *rest = buffer + 5;
                char *target = strtok(rest, " ");
                char *message = strtok(NULL, "");
                
                if (target && message) {
                    send_private_message(target, message);
                } else {
                    printf("Uso: /msg <nombre_usuario> <mensaje>\n");
                }
                
            } else if (strncmp(buffer, "/help", 5) == 0) {
                // Mostrar ayuda
                printf("\nComandos disponibles:\n");
                printf("  /list                 - Listar usuarios conectados\n");
                printf("  /info <usuario>       - Obtener información de un usuario\n");
                printf("  /status <estado>      - Cambiar estado (ACTIVO, OCUPADO, INACTIVO)\n");
                printf("  /msg <usuario> <msg>  - Enviar mensaje privado\n");
                printf("  /exit o /quit         - Salir del chat\n");
                printf("  /help                 - Mostrar esta ayuda\n");
                printf("\nCualquier otro texto se enviará como mensaje de difusión a todos los usuarios.\n");
                
            } else {
                printf("Comando desconocido. Usa /help para ver los comandos disponibles.\n");
            }
        } else {
            // Mensaje de broadcast
            send_broadcast_message(buffer);
        }
    }
    
    return NULL;
}

// Manejador para señales
void handle_signal(int sig) {
    printf("\nRecibida señal %d, finalizando...\n", sig);
    
    is_running = 0;
    
    // Si estamos conectados, enviar mensaje de desconexión
    if (client_socket >= 0) {
        send_disconnect_message();
        sleep(1); // Dar tiempo para que se envíe el mensaje
        close(client_socket);
    }
    
    printf("Saliendo...\n");
    exit(0);
}

// Función para conectar con el servidor
int connect_to_server() {
    struct sockaddr_in server_addr;
    struct hostent *server;
    
    // Crear socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Error al crear socket");
        return -1;
    }
    
    // Obtener información del servidor
    server = gethostbyname(server_ip);
    if (server == NULL) {
        fprintf(stderr, "Error: No se puede resolver el nombre del servidor %s\n", server_ip);
        close(client_socket);
        client_socket = -1;
        return -1;
    }
    
    // Configurar dirección del servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(server_port);
    
    // Conectar al servidor
    printf("Conectando a %s:%d...\n", server_ip, server_port);
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error al conectar");
        close(client_socket);
        client_socket = -1;
        return -1;
    }
    
    printf("Conexión establecida con el servidor\n");
    return 0;
}

int main(int argc, char *argv[]) {
    // Configurar manejadores de señales
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Si no se ha configurado la dirección IP del servidor, solicitarla
    char server_ip_buffer[100];
    if (server_ip == NULL) {
        printf("Ingresa la IP del servidor (o presiona Enter para usar '52.207.239.8'): ");
        fgets(server_ip_buffer, sizeof(server_ip_buffer), stdin);
        
        // Eliminar el salto de línea
        size_t ip_len = strlen(server_ip_buffer);
        if (ip_len > 0 && server_ip_buffer[ip_len - 1] == '\n') {
            server_ip_buffer[ip_len - 1] = '\0';
            ip_len--;
        }
        
        // Si no se ingresó nada, usar la IP por defecto
        if (ip_len == 0) {
            server_ip = strdup("52.207.239.8");
            printf("Usando IP por defecto: %s\n", server_ip);
        } else {
            server_ip = strdup(server_ip_buffer);
        }
    }
    
    // Solicitar nombre de usuario
    printf("Ingresa tu nombre de usuario: ");
    fgets(username, sizeof(username), stdin);
    
    // Eliminar el salto de línea
    size_t len = strlen(username);
    if (len > 0 && username[len - 1] == '\n') {
        username[len - 1] = '\0';
    }
    
    // Verificar que el nombre no esté vacío
    if (strlen(username) == 0) {
        printf("El nombre de usuario no puede estar vacío\n");
        return 1;
    }
    
    // Crear threads
    pthread_t receiver_tid, ui_tid;
    
    // Iniciar thread para recibir mensajes
    if (pthread_create(&receiver_tid, NULL, receiver_thread, NULL) != 0) {
        printf("Error al crear thread receptor\n");
        return 1;
    }
    
    // Iniciar thread para interfaz de usuario
    if (pthread_create(&ui_tid, NULL, user_interface_thread, NULL) != 0) {
        printf("Error al crear thread de interfaz de usuario\n");
        return 1;
    }
    
    // Bucle principal de reconexión
    while (is_running) {
        // Si no estamos conectados, intentar conectar
        if (client_socket < 0) {
            reconnect_attempts++;
            
            if (reconnect_attempts > MAX_RECONNECT_ATTEMPTS) {
                printf("\nMáximo número de intentos de reconexión alcanzado (%d)\n", MAX_RECONNECT_ATTEMPTS);
                printf("Verifica la IP y puerto del servidor e inténtalo de nuevo\n");
                is_running = 0;
                break;
            }
            
            // Intentar conectar
            if (connect_to_server() == 0) {
                // Reiniciar contador de reconexiones
                reconnect_attempts = 0;
                
                // Enviar mensaje de registro
                send_register_message();
                client_state = STATE_REGISTERING;
            } else {
                printf("Error al conectar. Reintentando en %d segundos (intento %d/%d)...\n", 
                       RECONNECT_DELAY, reconnect_attempts, MAX_RECONNECT_ATTEMPTS);
                sleep(RECONNECT_DELAY);
                continue;
            }
        }
        
        // Esperar un poco para no sobrecargar la CPU
        sleep(1);
    }
    
    // Esperar a que finalicen los threads
    if (client_socket >= 0) {
        close(client_socket);
    }
    
    pthread_join(receiver_tid, NULL);
    pthread_join(ui_tid, NULL);
    
    // Liberar memoria
    if (server_ip) {
        free(server_ip);
    }
    
    return 0;
}
