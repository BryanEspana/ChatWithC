#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <libwebsockets.h>
#include "cJSON.h"
#include <sys/time.h>

#define MAX_PAYLOAD 2048
#define RECONNECT_DELAY 5
#define MAX_RECONNECT_ATTEMPTS 5

// Estados del cliente
#define STATE_CONNECTING 0
#define STATE_REGISTERING 1
#define STATE_CONNECTED 2

// Información del servidor
// Actualiza esta IP con la dirección de tu nueva instancia EC2
char *server_ip = NULL;
int server_port = 8081;
int reconnect_attempts = 0;

// Nombre de usuario
char username[50];

// Estado del cliente
int client_state = STATE_CONNECTING;

// Contexto de LWS
struct lws_context *context = NULL;
struct lws *web_socket = NULL;

// Mutex para sincronización
pthread_mutex_t lock_established = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_established = PTHREAD_COND_INITIALIZER;

// Buffer para recibir mensajes
unsigned char rx_buffer[LWS_PRE + MAX_PAYLOAD];
unsigned int rx_buffer_len = 0;

// Flags para controlar la ejecución del programa
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

// Enviar mensaje de registro al servidor
void send_register_message(struct lws *wsi) {
    // Crear mensaje JSON simplificado - solo envía los campos absolutamente necesarios
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "register");
    cJSON_AddStringToObject(root, "sender", username);
    
    // Dejamos fuera el campo content y timestamp para simplificar el mensaje
    // y evitar posibles problemas de compatibilidad
    
    // Convertir a string
    char *json_str = cJSON_Print(root);
    
    // Preparar buffer para envío
    unsigned char buf[LWS_PRE + MAX_PAYLOAD];
    size_t json_len = strlen(json_str);
    
    // Verifica que el mensaje no sea demasiado grande
    if (json_len > MAX_PAYLOAD - 1) {
        printf("Error: Mensaje demasiado largo\n");
        cJSON_Delete(root);
        free(json_str);
        return;
    }
    
    // Copiar al buffer con offset LWS_PRE
    memcpy(&buf[LWS_PRE], json_str, json_len);
    
    // Enviar mensaje
    int n = lws_write(wsi, &buf[LWS_PRE], json_len, LWS_WRITE_TEXT);
    
    if (n < json_len) {
        printf("Error al enviar mensaje de registro\n");
    } else {
        printf("Enviando solicitud de registro...\n");
        client_state = STATE_REGISTERING;
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Enviar mensaje de broadcast
void send_broadcast_message(struct lws *wsi, const char *message) {
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
    
    // Preparar buffer para envío
    unsigned char buf[LWS_PRE + MAX_PAYLOAD];
    size_t json_len = strlen(json_str);
    
    // Verifica que el mensaje no sea demasiado grande
    if (json_len > MAX_PAYLOAD - 1) {
        printf("Error: Mensaje demasiado largo\n");
        cJSON_Delete(root);
        free(json_str);
        return;
    }
    
    // Copiar al buffer con offset LWS_PRE
    memcpy(&buf[LWS_PRE], json_str, json_len);
    
    // Enviar mensaje
    int n = lws_write(wsi, &buf[LWS_PRE], json_len, LWS_WRITE_TEXT);
    
    if (n < json_len) {
        printf("Error al enviar mensaje\n");
    } else {
        // Mostrar el mensaje enviado localmente para que el usuario vea su propio mensaje
        printf("\n[TÚ] [%s]: %s\n", timestamp, message);
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Enviar mensaje privado
void send_private_message(struct lws *wsi, const char *target, const char *message) {
    // Crear mensaje JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "broadcast");
    cJSON_AddStringToObject(root, "sender", username);
    cJSON_AddStringToObject(root, "target", target);
    cJSON_AddStringToObject(root, "content", message);
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);
    
    // Convertir a string
    char *json_str = cJSON_Print(root);
    
    // Preparar buffer para envío
    unsigned char buf[LWS_PRE + MAX_PAYLOAD];
    size_t json_len = strlen(json_str);
    
    // Verifica que el mensaje no sea demasiado grande
    if (json_len > MAX_PAYLOAD - 1) {
        printf("Error: Mensaje demasiado largo\n");
        cJSON_Delete(root);
        free(json_str);
        return;
    }
    
    // Copiar al buffer con offset LWS_PRE
    memcpy(&buf[LWS_PRE], json_str, json_len);
    
    // Enviar mensaje
    int n = lws_write(wsi, &buf[LWS_PRE], json_len, LWS_WRITE_TEXT);
    
    if (n < json_len) {
        printf("Error al enviar mensaje privado\n");
    } else {
        // Mostrar el mensaje enviado localmente para que el usuario vea su propio mensaje privado
        printf("\n[PRIVADO a %s] [%s]: %s\n", target, timestamp, message);
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Solicitar lista de usuarios
void request_user_list(struct lws *wsi) {
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
    
    // Preparar buffer para envío
    unsigned char buf[LWS_PRE + MAX_PAYLOAD];
    size_t json_len = strlen(json_str);
    
    // Copiar al buffer con offset LWS_PRE
    memcpy(&buf[LWS_PRE], json_str, json_len);
    
    // Enviar mensaje
    int n = lws_write(wsi, &buf[LWS_PRE], json_len, LWS_WRITE_TEXT);
    
    if (n < json_len) {
        printf("Error al solicitar lista de usuarios\n");
    } else {
        printf("Solicitando lista de usuarios...\n");
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Solicitar información de un usuario
void request_user_info(struct lws *wsi, const char *target) {
    // Crear mensaje JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "user_info");
    cJSON_AddStringToObject(root, "sender", username);
    cJSON_AddStringToObject(root, "target", target);
    cJSON_AddStringToObject(root, "content", "");
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    cJSON_AddStringToObject(root, "timestamp", timestamp);
    
    // Convertir a string
    char *json_str = cJSON_Print(root);
    
    // Preparar buffer para envío
    unsigned char buf[LWS_PRE + MAX_PAYLOAD];
    size_t json_len = strlen(json_str);
    
    // Copiar al buffer con offset LWS_PRE
    memcpy(&buf[LWS_PRE], json_str, json_len);
    
    // Enviar mensaje
    int n = lws_write(wsi, &buf[LWS_PRE], json_len, LWS_WRITE_TEXT);
    
    if (n < json_len) {
        printf("Error al solicitar información de usuario\n");
    } else {
        printf("Solicitando información del usuario %s...\n", target);
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Cambiar estado
void change_status(struct lws *wsi, const char *new_status) {
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
    
    // Preparar buffer para envío
    unsigned char buf[LWS_PRE + MAX_PAYLOAD];
    size_t json_len = strlen(json_str);
    
    // Copiar al buffer con offset LWS_PRE
    memcpy(&buf[LWS_PRE], json_str, json_len);
    
    // Enviar mensaje
    int n = lws_write(wsi, &buf[LWS_PRE], json_len, LWS_WRITE_TEXT);
    
    if (n < json_len) {
        printf("Error al cambiar estado\n");
    } else {
        printf("Cambiando estado a %s...\n", new_status);
    }
    
    cJSON_Delete(root);
    free(json_str);
}

// Enviar mensaje de desconexión
void send_disconnect_message(struct lws *wsi) {
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
    
    // Preparar buffer para envío
    unsigned char buf[LWS_PRE + MAX_PAYLOAD];
    size_t json_len = strlen(json_str);
    
    // Copiar al buffer con offset LWS_PRE
    memcpy(&buf[LWS_PRE], json_str, json_len);
    
    // Enviar mensaje
    int n = lws_write(wsi, &buf[LWS_PRE], json_len, LWS_WRITE_TEXT);
    
    if (n < json_len) {
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
        printf("Error al parsear JSON recibido\n");
        return;
    }
    
    // Obtener tipo de mensaje
    cJSON *type_item = cJSON_GetObjectItem(json, "type");
    if (!type_item || !cJSON_IsString(type_item)) {
        cJSON_Delete(json);
        return;
    }
    
    const char *type = type_item->valuestring;
    const char *sender = "";
    const char *content = "";
    const char *timestamp = "";
    
    // Obtener remitente si existe
    cJSON *sender_item = cJSON_GetObjectItem(json, "sender");
    if (sender_item && cJSON_IsString(sender_item)) {
        sender = sender_item->valuestring;
    }
    
    // Obtener contenido si existe
    cJSON *content_item = cJSON_GetObjectItem(json, "content");
    if (content_item) {
        if (cJSON_IsString(content_item)) {
            content = content_item->valuestring;
        } else if (cJSON_IsArray(content_item)) {
            // Para list_users_response, el contenido es un array
            content = ""; // Lo procesaremos específicamente más adelante
        }
    }
    
    // Obtener timestamp si existe
    cJSON *timestamp_item = cJSON_GetObjectItem(json, "timestamp");
    if (timestamp_item && cJSON_IsString(timestamp_item)) {
        timestamp = timestamp_item->valuestring;
    }
    
    // Procesar según el tipo de mensaje
    if (strcmp(type, "register_success") == 0) {
        printf("\n[SERVIDOR] Registro exitoso\n");
        client_state = STATE_CONNECTED;
        
        // Mostrar lista de usuarios conectados
        cJSON *user_list = cJSON_GetObjectItem(json, "userList");
        if (user_list && cJSON_IsArray(user_list)) {
            printf("Usuarios conectados:\n");
            int size = cJSON_GetArraySize(user_list);
            for (int i = 0; i < size; i++) {
                cJSON *user_item = cJSON_GetArrayItem(user_list, i);
                if (user_item && cJSON_IsString(user_item)) {
                    printf("  - %s\n", user_item->valuestring);
                }
            }
        }
    } else if (strcmp(type, "error") == 0) {
        printf("\n[ERROR] %s\n", content);
    } else if (strcmp(type, "broadcast") == 0) {
        printf("\n[DIFUSIÓN] [%s] %s: %s\n", timestamp, sender, content);
    } else if (strcmp(type, "private") == 0) {
        printf("\n[PRIVADO] [%s] %s: %s\n", timestamp, sender, content);
    } else if (strcmp(type, "list_users_response") == 0) {
        printf("\nLista de usuarios:\n");
        cJSON *users = content_item;
        if (users && cJSON_IsArray(users)) {
            int size = cJSON_GetArraySize(users);
            for (int i = 0; i < size; i++) {
                cJSON *user_item = cJSON_GetArrayItem(users, i);
                // Intentamos primero si es un objeto (formato nuevo)
                if (user_item && cJSON_IsObject(user_item)) {
                    cJSON *username_item = cJSON_GetObjectItem(user_item, "username");
                    if (username_item && cJSON_IsString(username_item)) {
                        printf("  - %s\n", username_item->valuestring);
                    }
                }
                // Si no es objeto, podría ser un string directo (formato antiguo)
                else if (user_item && cJSON_IsString(user_item)) {
                    printf("  - %s\n", user_item->valuestring);
                }
            }
        }
    } else if (strcmp(type, "user_info_response") == 0) {
        const char *target = "";
        cJSON *target_item = cJSON_GetObjectItem(json, "target");
        if (target_item && cJSON_IsString(target_item)) {
            target = target_item->valuestring;
        }
        
        printf("\nInformación del usuario %s:\n", target);
        
        cJSON *info = content_item;
        if (info && cJSON_IsObject(info)) {
            cJSON *ip_item = cJSON_GetObjectItem(info, "ip");
            cJSON *status_item = cJSON_GetObjectItem(info, "status");
            
            if (ip_item && cJSON_IsString(ip_item)) {
                printf("  - IP: %s\n", ip_item->valuestring);
            }
            
            if (status_item && cJSON_IsString(status_item)) {
                printf("  - Estado: %s\n", status_item->valuestring);
            }
        }
    } else if (strcmp(type, "status_update") == 0) {
        cJSON *status_content = content_item;
        if (status_content && cJSON_IsObject(status_content)) {
            cJSON *user_item = cJSON_GetObjectItem(status_content, "user");
            cJSON *status_item = cJSON_GetObjectItem(status_content, "status");
            
            if (user_item && cJSON_IsString(user_item) && 
                status_item && cJSON_IsString(status_item)) {
                printf("\n[ACTUALIZACIÓN] El usuario %s ahora está %s\n", 
                       user_item->valuestring, status_item->valuestring);
            }
        }
    } else if (strcmp(type, "user_connected") == 0) {
        printf("\n[SERVIDOR] %s\n", content);
    } else if (strcmp(type, "user_disconnected") == 0) {
        printf("\n[SERVIDOR] Usuario %s desconectado\n", content);
    }
    
    cJSON_Delete(json);
}

// Callback para WebSocket
static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason, 
                        void *user, void *in, size_t len) {
    static struct timeval last_ping_time;
    struct timeval now;
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("Conexión establecida con el servidor\n");
            
            // Notificar a cualquier thread esperando la conexión
            pthread_mutex_lock(&lock_established);
            pthread_cond_signal(&cond_established);
            pthread_mutex_unlock(&lock_established);
            
            // Resetear contador de reconexiones al establecer conexión
            reconnect_attempts = 0;
            
            // Inicializar tiempo del último ping
            gettimeofday(&last_ping_time, NULL);
            
            web_socket = wsi;
            
            // Enviar mensaje de registro
            send_register_message(wsi);
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            // Procesar el mensaje recibido
            if (in && len > 0) {
                char *message = malloc(len + 1);
                memcpy(message, in, len);
                message[len] = '\0';
                
                process_message(message, len);
                free(message);
            }
            break;
            
        // Añadir manejo de temporizadores para enviar pings y mantener la conexión activa
        case LWS_CALLBACK_TIMER:
            gettimeofday(&now, NULL);
            if (now.tv_sec - last_ping_time.tv_sec > 30) { // Enviar ping cada 30 segundos
                unsigned char ping[LWS_PRE + 10];
                memcpy(&ping[LWS_PRE], "ping", 4);
                lws_write(wsi, &ping[LWS_PRE], 4, LWS_WRITE_PING);
                
                gettimeofday(&last_ping_time, NULL);
            }
            break;
            
        case LWS_CALLBACK_CLIENT_CLOSED:
            printf("Conexión cerrada\n");
            web_socket = NULL;
            
            // Si todavía estamos ejecutando la aplicación, intentar reconectar
            if (is_running) {
                reconnect_attempts++;
                printf("Intento de reconexión %d/%d en %d segundos...\n", 
                       reconnect_attempts, MAX_RECONNECT_ATTEMPTS, RECONNECT_DELAY);
                
                if (reconnect_attempts > MAX_RECONNECT_ATTEMPTS) {
                    printf("\nMáximo número de intentos de reconexión alcanzado (%d)\n", MAX_RECONNECT_ATTEMPTS);
                    printf("Verifica la IP y puerto del servidor e inténtalo de nuevo\n");
                    is_running = 0;
                } else {
                    sleep(RECONNECT_DELAY);
                    client_state = STATE_CONNECTING;
                }
            }
            break;
            
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("Error de conexión: ");
            if (in && len > 0) {
                printf("%.*s", (int)len, (char*)in);
            } else {
                printf("desconocido");
            }
            printf("\n");
            
            web_socket = NULL;
            
            // Intentar reconectar
            if (is_running) {
                reconnect_attempts++;
                printf("Intento de reconexión %d/%d en %d segundos...\n", 
                       reconnect_attempts, MAX_RECONNECT_ATTEMPTS, RECONNECT_DELAY);
                
                if (reconnect_attempts > MAX_RECONNECT_ATTEMPTS) {
                    printf("\nMáximo número de intentos de reconexión alcanzado (%d)\n", MAX_RECONNECT_ATTEMPTS);
                    printf("Verifica la IP y puerto del servidor e inténtalo de nuevo\n");
                    is_running = 0;
                } else {
                    sleep(RECONNECT_DELAY);
                    client_state = STATE_CONNECTING;
                }
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Definición de protocolo
static struct lws_protocols protocols[] = {
    {
        "chat-protocol",
        callback_chat,
        0,
        MAX_PAYLOAD,
        0, NULL, 0
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 }
};

// Thread para la interfaz de usuario
void *user_interface_thread(void *arg) {
    char buffer[MAX_PAYLOAD];
    
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
                if (web_socket) {
                    send_disconnect_message(web_socket);
                }
                is_running = 0;
                break;
                
            } else if (strncmp(buffer, "/list", 5) == 0) {
                // Listar usuarios
                if (web_socket) {
                    request_user_list(web_socket);
                }
                
            } else if (strncmp(buffer, "/info ", 6) == 0) {
                // Obtener información de usuario
                char *target = buffer + 6;
                if (web_socket && strlen(target) > 0) {
                    request_user_info(web_socket, target);
                } else {
                    printf("Uso: /info <nombre_usuario>\n");
                }
                
            } else if (strncmp(buffer, "/status ", 8) == 0) {
                // Cambiar estado
                char *new_status = buffer + 8;
                if (web_socket && strlen(new_status) > 0) {
                    if (strcmp(new_status, "ACTIVO") == 0 || 
                        strcmp(new_status, "OCUPADO") == 0 || 
                        strcmp(new_status, "INACTIVO") == 0) {
                        change_status(web_socket, new_status);
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
                
                if (web_socket && target && message) {
                    send_private_message(web_socket, target, message);
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
            if (web_socket) {
                send_broadcast_message(web_socket, buffer);
            }
        }
    }
    
    return NULL;
}

// Manejador para señales
void handle_signal(int sig) {
    is_running = 0;
    
    // Si estamos conectados, enviar mensaje de desconexión
    if (web_socket) {
        send_disconnect_message(web_socket);
    }
    
    // Dar tiempo para que el mensaje se envíe
    usleep(500000);
    
    if (context) {
        lws_context_destroy(context);
        context = NULL;
    }
    
    printf("\nSaliendo...\n");
    exit(0);
}

int main(int argc, char **argv) {
    // Configurar manejadores de señales
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Si no se ha configurado la dirección IP del servidor, solicitarla
    char server_ip_buffer[100];
    if (server_ip == NULL) {
        printf("Ingresa la IP del servidor (o presiona Enter para usar 'localhost'): ");
        fgets(server_ip_buffer, sizeof(server_ip_buffer), stdin);
        
        // Eliminar el salto de línea
        size_t ip_len = strlen(server_ip_buffer);
        if (ip_len > 0 && server_ip_buffer[ip_len - 1] == '\n') {
            server_ip_buffer[ip_len - 1] = '\0';
            ip_len--;
        }
        
        // Si no se ingresó nada, usar la IP de la instancia EC2
        if (ip_len == 0) {
            server_ip = strdup("52.207.239.8");
            printf("Usando la IP de la instancia EC2 por defecto: %s\n", server_ip);
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
    
    // Crear thread para la interfaz de usuario
    pthread_t ui_thread;
    if (pthread_create(&ui_thread, NULL, user_interface_thread, NULL) != 0) {
        printf("Error al crear thread de interfaz de usuario\n");
        return 1;
    }
    
    // Configurar LWS
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.timeout_secs = 10; // Timeout más corto para detectar problemas de conexión más rápido
    info.ka_time = 30;      // Keep-alive cada 30 segundos
    info.ka_interval = 5;   // Intervalos de 5 segundos
    info.ka_probes = 3;     // 3 intentos antes de considerar la conexión perdida
    
    // Crear contexto
    context = lws_create_context(&info);
    if (!context) {
        printf("Error al crear contexto de libwebsockets\n");
        return 1;
    }
    
    // Bucle principal
    while (is_running) {
        // Si no estamos conectados, intentar conectar
        if (!web_socket) {
            // Configurar conexión
            struct lws_client_connect_info ccinfo;
            memset(&ccinfo, 0, sizeof(ccinfo));
            
            ccinfo.context = context;
            ccinfo.address = server_ip;
            ccinfo.port = server_port;
            ccinfo.path = "/";
            ccinfo.host = lws_canonical_hostname(context);
            ccinfo.origin = "origin";
            ccinfo.protocol = "chat-protocol";
            ccinfo.ssl_connection = 0; // No usar SSL/TLS para esta conexión
            
            printf("Conectando a %s:%d...\n", server_ip, server_port);
            web_socket = lws_client_connect_via_info(&ccinfo);
            
            if (!web_socket) {
                printf("Error al iniciar conexión\n");
                reconnect_attempts++;
                if (reconnect_attempts > MAX_RECONNECT_ATTEMPTS) {
                    printf("No se pudo establecer conexión después de %d intentos\n", MAX_RECONNECT_ATTEMPTS);
                    printf("Verifica que el servidor esté ejecutándose en %s:%d\n", server_ip, server_port);
                    printf("Comprueba que el servidor esté escuchando en todas las interfaces (0.0.0.0) y no solo en localhost\n");
                    is_running = 0;
                } else {
                    printf("Intento de reconexión %d/%d en %d segundos...\n", 
                           reconnect_attempts, MAX_RECONNECT_ATTEMPTS, RECONNECT_DELAY);
                    sleep(RECONNECT_DELAY);
                }
                continue;
            }
        }
        
        // Servicio LWS con manejo de reconexiones
        if (force_reconnect && web_socket) {
            printf("Forzando reconexión...\n");
            lws_callback_on_writable(web_socket); // Flush mensajes pendientes
            web_socket = NULL;
            force_reconnect = 0;
            continue;
        }
        
        // Servicio LWS
        lws_service(context, 50);
    }
    
    // Limpiar
    if (context) {
        lws_context_destroy(context);
    }
    
    pthread_join(ui_thread, NULL);
    
    return 0;
}
