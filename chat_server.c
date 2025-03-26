#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>
#include "cJSON.h"
#include <libwebsockets.h>

// Definición de estados de usuario
#define STATUS_ACTIVE "ACTIVO"
#define STATUS_BUSY "OCUPADO"
#define STATUS_INACTIVE "INACTIVO"

// Definición de tipos de mensajes
#define TYPE_REGISTER "register"
#define TYPE_BROADCAST "broadcast"
#define TYPE_PRIVATE "private"
#define TYPE_LIST_USERS "list_users"
#define TYPE_USER_INFO "user_info"
#define TYPE_CHANGE_STATUS "change_status"
#define TYPE_DISCONNECT "disconnect"
#define TYPE_ERROR "error"

// Tiempo después del cual un usuario se marca como inactivo (en segundos)
#define INACTIVITY_TIMEOUT 300

// Estructura para almacenar información de usuario
typedef struct {
    char username[50];
    char ip[INET_ADDRSTRLEN];
    char status[20];
    struct lws *wsi;
    time_t last_activity;
    bool is_active;
} User;

// Lista de usuarios y mutex para acceso concurrente
User *users = NULL;
int user_count = 0;
pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER;

// Estructura para pasar datos al thread de inactividad
typedef struct {
    int check_interval;
} InactivityParams;

// Genera timestamp en formato ISO 8601
void get_timestamp(char *timestamp, size_t size) {
    time_t now;
    struct tm *timeinfo;
    
    time(&now);
    timeinfo = localtime(&now);
    
    strftime(timestamp, size, "%Y-%m-%dT%H:%M:%S", timeinfo);
}

// Busca un usuario por nombre
int find_user_by_name(const char *username) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0 && users[i].is_active) {
            return i;
        }
    }
    return -1;
}

// Busca un usuario por su WebSocket
int find_user_by_wsi(struct lws *wsi) {
    for (int i = 0; i < user_count; i++) {
        if (users[i].wsi == wsi && users[i].is_active) {
            return i;
        }
    }
    return -1;
}

// Añade un nuevo usuario
int add_user(const char *username, const char *ip, struct lws *wsi) {
    pthread_mutex_lock(&users_mutex);
    
    // Verificar si el usuario ya existe
    if (find_user_by_name(username) != -1) {
        pthread_mutex_unlock(&users_mutex);
        return -1;
    }
    
    // Reservar memoria para un usuario adicional
    users = realloc(users, (user_count + 1) * sizeof(User));
    if (!users) {
        pthread_mutex_unlock(&users_mutex);
        return -2;
    }
    
    // Inicializar nuevo usuario
    strncpy(users[user_count].username, username, sizeof(users[user_count].username) - 1);
    users[user_count].username[sizeof(users[user_count].username) - 1] = '\0';
    
    strncpy(users[user_count].ip, ip, sizeof(users[user_count].ip) - 1);
    users[user_count].ip[sizeof(users[user_count].ip) - 1] = '\0';
    
    strncpy(users[user_count].status, STATUS_ACTIVE, sizeof(users[user_count].status) - 1);
    users[user_count].status[sizeof(users[user_count].status) - 1] = '\0';
    
    users[user_count].wsi = wsi;
    time(&users[user_count].last_activity);
    users[user_count].is_active = true;
    
    user_count++;
    
    pthread_mutex_unlock(&users_mutex);
    return 0;
}

// Elimina un usuario
void remove_user(struct lws *wsi) {
    pthread_mutex_lock(&users_mutex);
    
    int index = find_user_by_wsi(wsi);
    if (index != -1) {
        users[index].is_active = false;
        printf("Usuario %s desconectado\n", users[index].username);
    }
    
    pthread_mutex_unlock(&users_mutex);
}

// Actualiza el estado de un usuario
int update_user_status(const char *username, const char *new_status) {
    pthread_mutex_lock(&users_mutex);
    
    int index = find_user_by_name(username);
    if (index == -1) {
        pthread_mutex_unlock(&users_mutex);
        return -1;
    }
    
    strncpy(users[index].status, new_status, sizeof(users[index].status) - 1);
    users[index].status[sizeof(users[index].status) - 1] = '\0';
    time(&users[index].last_activity);
    
    pthread_mutex_unlock(&users_mutex);
    return 0;
}

// Actualiza tiempo de última actividad para un usuario
void update_user_activity(struct lws *wsi) {
    pthread_mutex_lock(&users_mutex);
    
    int index = find_user_by_wsi(wsi);
    if (index != -1) {
        time(&users[index].last_activity);
    }
    
    pthread_mutex_unlock(&users_mutex);
}

// Prepara un mensaje para ser enviado por WebSocket
unsigned char *prepare_message(const char *json, size_t *len) {
    // Calcula el tamaño total del mensaje (LWS_PRE para headers)
    size_t message_size = strlen(json) + LWS_PRE;
    unsigned char *message = malloc(message_size);
    
    if (!message)
        return NULL;
    
    // Copia el mensaje después de LWS_PRE bytes (para headers de WebSocket)
    memcpy(message + LWS_PRE, json, strlen(json));
    *len = strlen(json);
    
    return message;
}

// Envía respuesta de error
void send_error(struct lws *wsi, const char *error_message) {
    cJSON *error = cJSON_CreateObject();
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    
    cJSON_AddStringToObject(error, "type", TYPE_ERROR);
    cJSON_AddStringToObject(error, "sender", "server");
    cJSON_AddStringToObject(error, "content", error_message);
    cJSON_AddStringToObject(error, "timestamp", timestamp);
    
    char *json_str = cJSON_Print(error);
    size_t len;
    unsigned char *response = prepare_message(json_str, &len);
    
    if (response) {
        lws_write(wsi, response + LWS_PRE, len, LWS_WRITE_TEXT);
        free(response);
    }
    
    cJSON_Delete(error);
    free(json_str);
}

// Procesa mensaje de registro
void handle_register(struct lws *wsi, cJSON *json) {
    // Validar que el JSON contiene el campo "sender"
    cJSON *sender_field = cJSON_GetObjectItem(json, "sender");
    if (!sender_field || !cJSON_IsString(sender_field) || !sender_field->valuestring) {
        send_error(wsi, "Formato de mensaje inválido: falta campo 'sender' o no es un string");
        return;
    }
    
    const char *username = sender_field->valuestring;
    char client_ip[INET_ADDRSTRLEN];
    
    // Imprimir mensaje de depuración
    printf("Procesando registro para usuario: %s\n", username);
    
    // Usar un valor fijo para la IP del cliente para evitar problemas
    strncpy(client_ip, "127.0.0.1", sizeof(client_ip) - 1);
    client_ip[sizeof(client_ip) - 1] = '\0';
    
    printf("Usando IP estándar: %s para el cliente\n", client_ip);
    
    printf("IP del cliente: %s\n", client_ip);
    
    int result = add_user(username, client_ip, wsi);
    
    if (result == -1) {
        send_error(wsi, "Nombre de usuario ya existe");
        return;
    } else if (result == -2) {
        send_error(wsi, "Error de memoria al registrar usuario");
        return;
    }
    
    // Crear lista de usuarios para respuesta
    cJSON *response = cJSON_CreateObject();
    cJSON *userList = cJSON_CreateArray();
    
    pthread_mutex_lock(&users_mutex);
    for (int i = 0; i < user_count; i++) {
        if (users[i].is_active) {
            cJSON_AddItemToArray(userList, cJSON_CreateString(users[i].username));
        }
    }
    pthread_mutex_unlock(&users_mutex);
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    
    cJSON_AddStringToObject(response, "type", "register_success");
    cJSON_AddStringToObject(response, "sender", "server");
    cJSON_AddStringToObject(response, "content", "Registro exitoso");
    cJSON_AddItemToObject(response, "userList", userList);
    cJSON_AddStringToObject(response, "timestamp", timestamp);
    
    char *json_str = cJSON_Print(response);
    size_t len;
    unsigned char *resp_message = prepare_message(json_str, &len);
    
    if (resp_message) {
        lws_write(wsi, resp_message + LWS_PRE, len, LWS_WRITE_TEXT);
        free(resp_message);
    }
    
    cJSON_Delete(response);
    free(json_str);
    
    // Notificar a todos los usuarios sobre el nuevo usuario
    char notification[256];
    snprintf(notification, sizeof(notification), "%s se ha conectado", username);
    
    cJSON *broadcast = cJSON_CreateObject();
    cJSON_AddStringToObject(broadcast, "type", "user_connected");
    cJSON_AddStringToObject(broadcast, "sender", "server");
    cJSON_AddStringToObject(broadcast, "content", notification);
    cJSON_AddStringToObject(broadcast, "timestamp", timestamp);
    
    json_str = cJSON_Print(broadcast);
    len = 0;
    unsigned char *broadcast_msg = prepare_message(json_str, &len);
    
    if (broadcast_msg) {
        pthread_mutex_lock(&users_mutex);
        for (int i = 0; i < user_count; i++) {
            if (users[i].is_active && users[i].wsi != wsi) {
                lws_write(users[i].wsi, broadcast_msg + LWS_PRE, len, LWS_WRITE_TEXT);
            }
        }
        pthread_mutex_unlock(&users_mutex);
        free(broadcast_msg);
    }
    
    cJSON_Delete(broadcast);
    free(json_str);
}

// Procesa mensaje de broadcast
void handle_broadcast(struct lws *wsi, cJSON *json) {
    const char *sender = cJSON_GetObjectItem(json, "sender")->valuestring;
    const char *content = cJSON_GetObjectItem(json, "content")->valuestring;
    
    int sender_index = find_user_by_name(sender);
    if (sender_index == -1 || users[sender_index].wsi != wsi) {
        send_error(wsi, "Autenticación fallida para broadcast");
        return;
    }
    
    // Actualizar actividad del usuario
    update_user_activity(wsi);
    
    // Reenviar mensaje a todos los usuarios activos
    char *json_str = cJSON_Print(json);
    size_t len;
    unsigned char *broadcast_msg = prepare_message(json_str, &len);
    
    if (broadcast_msg) {
        pthread_mutex_lock(&users_mutex);
        for (int i = 0; i < user_count; i++) {
            if (users[i].is_active) {
                lws_write(users[i].wsi, broadcast_msg + LWS_PRE, len, LWS_WRITE_TEXT);
            }
        }
        pthread_mutex_unlock(&users_mutex);
        free(broadcast_msg);
    }
    
    free(json_str);
}

// Procesa mensaje privado
void handle_private(struct lws *wsi, cJSON *json) {
    const char *sender = cJSON_GetObjectItem(json, "sender")->valuestring;
    const char *target = cJSON_GetObjectItem(json, "target")->valuestring;
    const char *content = cJSON_GetObjectItem(json, "content")->valuestring;
    
    int sender_index = find_user_by_name(sender);
    if (sender_index == -1 || users[sender_index].wsi != wsi) {
        send_error(wsi, "Autenticación fallida para mensaje privado");
        return;
    }
    
    // Actualizar actividad del usuario
    update_user_activity(wsi);
    
    // Buscar destinatario
    int target_index = find_user_by_name(target);
    if (target_index == -1) {
        send_error(wsi, "Usuario destinatario no encontrado");
        return;
    }
    
    // Enviar mensaje al remitente y al destinatario
    char *json_str = cJSON_Print(json);
    size_t len;
    unsigned char *message = prepare_message(json_str, &len);
    
    if (message) {
        lws_write(wsi, message + LWS_PRE, len, LWS_WRITE_TEXT);
        lws_write(users[target_index].wsi, message + LWS_PRE, len, LWS_WRITE_TEXT);
        free(message);
    }
    
    free(json_str);
}

// Procesa solicitud de lista de usuarios
void handle_list_users(struct lws *wsi, cJSON *json) {
    const char *sender = cJSON_GetObjectItem(json, "sender")->valuestring;
    
    int sender_index = find_user_by_name(sender);
    if (sender_index == -1 || users[sender_index].wsi != wsi) {
        send_error(wsi, "Autenticación fallida para listar usuarios");
        return;
    }
    
    // Actualizar actividad del usuario
    update_user_activity(wsi);
    
    // Crear lista de usuarios
    cJSON *response = cJSON_CreateObject();
    cJSON *user_list = cJSON_CreateArray();
    
    pthread_mutex_lock(&users_mutex);
    for (int i = 0; i < user_count; i++) {
        if (users[i].is_active) {
            cJSON_AddItemToArray(user_list, cJSON_CreateString(users[i].username));
        }
    }
    pthread_mutex_unlock(&users_mutex);
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    
    cJSON_AddStringToObject(response, "type", "list_users_response");
    cJSON_AddStringToObject(response, "sender", "server");
    cJSON_AddItemToObject(response, "content", user_list);
    cJSON_AddStringToObject(response, "timestamp", timestamp);
    
    char *json_str = cJSON_Print(response);
    size_t len;
    unsigned char *resp_message = prepare_message(json_str, &len);
    
    if (resp_message) {
        lws_write(wsi, resp_message + LWS_PRE, len, LWS_WRITE_TEXT);
        free(resp_message);
    }
    
    cJSON_Delete(response);
    free(json_str);
}

// Procesa solicitud de información de usuario
void handle_user_info(struct lws *wsi, cJSON *json) {
    const char *sender = cJSON_GetObjectItem(json, "sender")->valuestring;
    const char *target = cJSON_GetObjectItem(json, "target")->valuestring;
    
    int sender_index = find_user_by_name(sender);
    if (sender_index == -1 || users[sender_index].wsi != wsi) {
        send_error(wsi, "Autenticación fallida para información de usuario");
        return;
    }
    
    // Actualizar actividad del usuario
    update_user_activity(wsi);
    
    // Buscar usuario objetivo
    int target_index = find_user_by_name(target);
    if (target_index == -1) {
        send_error(wsi, "Usuario objetivo no encontrado");
        return;
    }
    
    // Preparar información de usuario
    cJSON *response = cJSON_CreateObject();
    cJSON *user_info = cJSON_CreateObject();
    
    cJSON_AddStringToObject(user_info, "ip", users[target_index].ip);
    cJSON_AddStringToObject(user_info, "status", users[target_index].status);
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    
    cJSON_AddStringToObject(response, "type", "user_info_response");
    cJSON_AddStringToObject(response, "sender", "server");
    cJSON_AddStringToObject(response, "target", target);
    cJSON_AddItemToObject(response, "content", user_info);
    cJSON_AddStringToObject(response, "timestamp", timestamp);
    
    char *json_str = cJSON_Print(response);
    size_t len;
    unsigned char *resp_message = prepare_message(json_str, &len);
    
    if (resp_message) {
        lws_write(wsi, resp_message + LWS_PRE, len, LWS_WRITE_TEXT);
        free(resp_message);
    }
    
    cJSON_Delete(response);
    free(json_str);
}

// Procesa solicitud de cambio de estado
void handle_change_status(struct lws *wsi, cJSON *json) {
    const char *sender = cJSON_GetObjectItem(json, "sender")->valuestring;
    const char *new_status = cJSON_GetObjectItem(json, "content")->valuestring;
    
    int sender_index = find_user_by_name(sender);
    if (sender_index == -1 || users[sender_index].wsi != wsi) {
        send_error(wsi, "Autenticación fallida para cambio de estado");
        return;
    }
    
    // Validar estado
    if (strcmp(new_status, STATUS_ACTIVE) != 0 && 
        strcmp(new_status, STATUS_BUSY) != 0 && 
        strcmp(new_status, STATUS_INACTIVE) != 0) {
        send_error(wsi, "Estado no válido");
        return;
    }
    
    // Actualizar estado
    update_user_status(sender, new_status);
    
    // Notificar a todos los usuarios
    cJSON *status_update = cJSON_CreateObject();
    cJSON *status_content = cJSON_CreateObject();
    
    cJSON_AddStringToObject(status_content, "user", sender);
    cJSON_AddStringToObject(status_content, "status", new_status);
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    
    cJSON_AddStringToObject(status_update, "type", "status_update");
    cJSON_AddStringToObject(status_update, "sender", "server");
    cJSON_AddItemToObject(status_update, "content", status_content);
    cJSON_AddStringToObject(status_update, "timestamp", timestamp);
    
    char *json_str = cJSON_Print(status_update);
    size_t len;
    unsigned char *update_msg = prepare_message(json_str, &len);
    
    if (update_msg) {
        pthread_mutex_lock(&users_mutex);
        for (int i = 0; i < user_count; i++) {
            if (users[i].is_active) {
                lws_write(users[i].wsi, update_msg + LWS_PRE, len, LWS_WRITE_TEXT);
            }
        }
        pthread_mutex_unlock(&users_mutex);
        free(update_msg);
    }
    
    cJSON_Delete(status_update);
    free(json_str);
}

// Procesa solicitud de desconexión
void handle_disconnect(struct lws *wsi, cJSON *json) {
    const char *sender = cJSON_GetObjectItem(json, "sender")->valuestring;
    
    int sender_index = find_user_by_name(sender);
    if (sender_index == -1 || users[sender_index].wsi != wsi) {
        send_error(wsi, "Autenticación fallida para desconexión");
        return;
    }
    
    // Notificar a todos los usuarios
    cJSON *disconnect_notice = cJSON_CreateObject();
    
    char timestamp[30];
    get_timestamp(timestamp, sizeof(timestamp));
    
    cJSON_AddStringToObject(disconnect_notice, "type", "user_disconnected");
    cJSON_AddStringToObject(disconnect_notice, "sender", "server");
    cJSON_AddStringToObject(disconnect_notice, "content", sender);
    cJSON_AddStringToObject(disconnect_notice, "timestamp", timestamp);
    
    char *json_str = cJSON_Print(disconnect_notice);
    size_t len;
    unsigned char *notice_msg = prepare_message(json_str, &len);
    
    if (notice_msg) {
        pthread_mutex_lock(&users_mutex);
        for (int i = 0; i < user_count; i++) {
            if (users[i].is_active && users[i].wsi != wsi) {
                lws_write(users[i].wsi, notice_msg + LWS_PRE, len, LWS_WRITE_TEXT);
            }
        }
        pthread_mutex_unlock(&users_mutex);
        free(notice_msg);
    }
    
    cJSON_Delete(disconnect_notice);
    free(json_str);
    
    // Marcar como inactivo
    remove_user(wsi);
}

// Thread para verificar inactividad de usuarios
void *check_inactivity(void *param) {
    InactivityParams *params = (InactivityParams*)param;
    
    while (1) {
        sleep(params->check_interval);
        
        time_t current_time;
        time(&current_time);
        
        pthread_mutex_lock(&users_mutex);
        for (int i = 0; i < user_count; i++) {
            if (users[i].is_active && 
                strcmp(users[i].status, STATUS_INACTIVE) != 0 && 
                difftime(current_time, users[i].last_activity) > INACTIVITY_TIMEOUT) {
                
                // Cambiar a estado inactivo
                strncpy(users[i].status, STATUS_INACTIVE, sizeof(users[i].status) - 1);
                users[i].status[sizeof(users[i].status) - 1] = '\0';
                
                // Notificar a todos
                cJSON *status_update = cJSON_CreateObject();
                cJSON *status_content = cJSON_CreateObject();
                
                cJSON_AddStringToObject(status_content, "user", users[i].username);
                cJSON_AddStringToObject(status_content, "status", STATUS_INACTIVE);
                
                char timestamp[30];
                get_timestamp(timestamp, sizeof(timestamp));
                
                cJSON_AddStringToObject(status_update, "type", "status_update");
                cJSON_AddStringToObject(status_update, "sender", "server");
                cJSON_AddItemToObject(status_update, "content", status_content);
                cJSON_AddStringToObject(status_update, "timestamp", timestamp);
                
                char *json_str = cJSON_Print(status_update);
                size_t len;
                unsigned char *update_msg = prepare_message(json_str, &len);
                
                if (update_msg) {
                    for (int j = 0; j < user_count; j++) {
                        if (users[j].is_active) {
                            lws_write(users[j].wsi, update_msg + LWS_PRE, len, LWS_WRITE_TEXT);
                        }
                    }
                    free(update_msg);
                }
                
                cJSON_Delete(status_update);
                free(json_str);
            }
        }
        pthread_mutex_unlock(&users_mutex);
    }
    
    free(params);
    return NULL;
}

// Callback principal para WebSocket
static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("WebSocket conexión establecida\n");
            break;
            
        case LWS_CALLBACK_RECEIVE:
            if (len == 0)
                break;
                
            // Parsear JSON
            char *message = malloc(len + 1);
            if (!message)
                break;
                
            memcpy(message, in, len);
            message[len] = '\0';
            
            cJSON *json = cJSON_Parse(message);
            free(message);
            
            if (!json) {
                send_error(wsi, "Formato JSON inválido");
                break;
            }
            
            // Procesar mensaje según tipo
            cJSON *type_item = cJSON_GetObjectItem(json, "type");
            if (!type_item || !cJSON_IsString(type_item)) {
                send_error(wsi, "Falta campo 'type' en mensaje");
                cJSON_Delete(json);
                break;
            }
            
            const char *type = type_item->valuestring;
            
            if (strcmp(type, TYPE_REGISTER) == 0) {
                handle_register(wsi, json);
            } else if (strcmp(type, TYPE_BROADCAST) == 0) {
                handle_broadcast(wsi, json);
            } else if (strcmp(type, TYPE_PRIVATE) == 0) {
                handle_private(wsi, json);
            } else if (strcmp(type, TYPE_LIST_USERS) == 0) {
                handle_list_users(wsi, json);
            } else if (strcmp(type, TYPE_USER_INFO) == 0) {
                handle_user_info(wsi, json);
            } else if (strcmp(type, TYPE_CHANGE_STATUS) == 0) {
                handle_change_status(wsi, json);
            } else if (strcmp(type, TYPE_DISCONNECT) == 0) {
                handle_disconnect(wsi, json);
            } else {
                send_error(wsi, "Tipo de mensaje desconocido");
            }
            
            cJSON_Delete(json);
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("WebSocket conexión cerrada\n");
            remove_user(wsi);
            break;
            
        default:
            break;
    }
    
    return 0;
}

// Definición de protocolos
static struct lws_protocols protocols[] = {
    {
        "chat-protocol",      // Nombre del protocolo
        callback_chat,        // Callback
        0,                    // Per session data size
        4096,                 // Tamaño máximo de payload rx
        0, NULL, 0            // Reservado
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 }  // Terminador
};

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <puerto>\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        printf("Puerto inválido. Debe estar entre 1 y 65535\n");
        return 1;
    }
    
    // Inicializar libwebsockets
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = port;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;
    
    struct lws_context *context = lws_create_context(&info);
    if (!context) {
        printf("Error al crear contexto de libwebsockets\n");
        return 1;
    }
    
    printf("Servidor iniciado en puerto %d\n", port);
    
    // Iniciar thread para verificar inactividad
    pthread_t inactivity_thread;
    InactivityParams *params = malloc(sizeof(InactivityParams));
    params->check_interval = 60;  // Verificar cada 60 segundos
    
    if (pthread_create(&inactivity_thread, NULL, check_inactivity, params) != 0) {
        printf("Error al crear thread de inactividad\n");
        free(params);
        lws_context_destroy(context);
        return 1;
    }
    
    // Bucle principal del servidor
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    free(users);
    return 0;
}