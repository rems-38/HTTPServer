#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "reponse.h"
#include "../api/api.h"
#include "config.h"

uint32_t h1(uint32_t code) { return code % NB_HTTP_CODES; }
uint32_t h2(uint32_t code) { return 1 + (code % (NB_HTTP_CODES - 1)); }
uint32_t hash(uint32_t code, uint32_t nbTry) {
    return (h1(code) + nbTry * h2(code)) % NB_HTTP_CODES;
}

void initTable(HTTPTable *codes) {
    for (int i = 0; i < NB_HTTP_CODES; i++) { codes->table[i] = NULL; }
    
    Header headers[] = {
        {"Content-Type", ""},
        {"Content-Length", ""},
        {"Connection", ""},
        {"Host", ""}
    };
    int headersCount = sizeof(headers) / sizeof(headers[0]);

    codes->filename = NULL;
    codes->is_head = false;
    codes->is_php = false;

    codes->headers = malloc(headersCount * sizeof(Header));
    for (int i = 0; i < headersCount; i++) {
        codes->headers[i].label = malloc(strlen(headers[i].label) + 1);
        strcpy(codes->headers[i].label, headers[i].label);
        codes->headers[i].value = malloc(strlen(headers[i].value) + 1);
        strcpy(codes->headers[i].value, headers[i].value);
    }
    codes->headersCount = headersCount;
}

void freeTable(HTTPTable *codes) {
    for (int i = 0; i < NB_HTTP_CODES; i++) {
        if (codes->table[i] != NULL) {
            free(codes->table[i]->info);
            free(codes->table[i]);
        }
    }
    free(codes->headers);
}

void addTable(HTTPTable *codes, int code, char *info) {
    HttpCode *el = malloc(sizeof(HttpCode));
    
    el->code = code;
    
    el->info = malloc(strlen(info) + 1);
    strcpy(el->info, info);

    int nbTry = 0;
    while (codes->table[hash(code, nbTry)] != NULL) { nbTry++; }
    codes->table[hash(code, nbTry)] = el;
}

HTTPTable *loadTable() {
    HTTPTable *codes = malloc(sizeof(HTTPTable));
    initTable(codes);

    addTable(codes, 200, "OK");
    addTable(codes, 201, "Created");
    addTable(codes, 202, "Accepted");
    addTable(codes, 204, "No Content");
    addTable(codes, 400, "Bad Request");
    addTable(codes, 401, "Unauthorized");
    addTable(codes, 403, "Forbidden");
    addTable(codes, 404, "Not Found");
    addTable(codes, 405, "Method Not Allowed");
    addTable(codes, 411, "Length Required");
    addTable(codes, 414, "URI Too Long");
    addTable(codes, 500, "Internal Server Error");
    addTable(codes, 501, "Not Implemented");
    addTable(codes, 503, "Service Unavailable");
    addTable(codes, 505, "HTTP Version Not Supported");

    return codes;
}

HttpReponse *getTable(HTTPTable *codes, int code) {
    HttpReponse *rep = malloc(sizeof(HttpReponse));

    int nbTry = 0;
    while (codes->table[hash(code, nbTry)]->code != code) { nbTry++; }

    rep->code = codes->table[hash(code, nbTry)];
    rep->httpminor = codes->httpminor;
    rep->filename = codes->filename;
    rep->is_head = codes->is_head;
    rep->headers = codes->headers;
    rep->headersCount = codes->headersCount;

    return rep;
}


message *createMsgFromReponse(HttpReponse rep, unsigned int clientId) {
    message *msg = malloc(sizeof(message));

    // Taille du fichier si existe
    FILE *fout = NULL;
    long fileSize = 0;
    if (rep.filename != NULL && !rep.is_head) {
        fout = fopen(rep.filename, "r");
        if (fout != NULL) {
            fseek(fout, 0, SEEK_END);
            fileSize = ftell(fout);
            fseek(fout, 0, SEEK_SET);
        }
    }

    // Calcul de la taille nécessaire pour buf
    int bufSize = strlen("HTTP/1.x ") + 3 + strlen(" ") + strlen(rep.code->info) + strlen("\r\n"); // 3 : taille d'une code (200, 404, ...)
    for (int i = 0; i < rep.headersCount; i++) {
        if (!(strcmp(rep.headers[i].value, "") == 0)) {
            bufSize += strlen(rep.headers[i].label) + 2 + strlen(rep.headers[i].value) + strlen("\r\n"); // +2 pour le ": "
        }
    }
    if (!rep.is_head) { bufSize += fileSize; }
    bufSize += 2 * strlen("\r\n");
    msg->buf = malloc(bufSize + 10);

    // Transfert de la data
    sprintf(msg->buf, "HTTP/1.%d %d %s\n", rep.httpminor, rep.code->code, rep.code->info);
    int len = strlen(msg->buf);
    for (int i = 0; i < rep.headersCount; i++) {
        if (!(strcmp(rep.headers[i].value, "") == 0)) {
            sprintf(msg->buf+len, "%s: %s\r\n", rep.headers[i].label, rep.headers[i].value);
            len += strlen(rep.headers[i].label) + 2 + strlen(rep.headers[i].value) + strlen("\r\n");
        }
    }
    sprintf(msg->buf+len, "\r\n");
    len += strlen("\r\n");

    if (fout != NULL && !rep.is_head) {
        fread(msg->buf+len, fileSize, 1, fout);
        len += fileSize;
    }
    sprintf(msg->buf+len, "\r\n");
    
    msg->len = bufSize;
    msg->clientId = clientId;

    return msg;
}

int hexa(char c){
    if(c >= 48 && c <= 57){ //chiffre   
        return c-48;
    }
    else{ //lettre
        return c-55;
    }
}


char* percentEncoding(char* uri){
    int len = strlen(uri);
    int k = 0;
    int j = 0;
    char* dest = malloc(len + 1);

    while(k < len){
        if(uri[k] == '%'){
            int HEX1 = hexa(uri[k+1]);
            int HEX2 = hexa(uri[k+2]);
            dest[j] = 16*HEX1 + HEX2;
            k += 3;
        }

        else{
            dest[j] = uri[k];
            k++;
        }
        j++;
    }
    dest[j] = '\0';
    return dest;
}


char* DotRemovalSegment(char* uri){
    int n = strlen(uri);
    
    char* out = malloc(n+1);
    int i = 0, j = 0;
    while(i < n){
        /*printf("\nout :");
        for(int k = 0; k < j; k++){
            printf("%c",out[k]);
        }
        printf("\turi :");
        for(int k = i; k < n; k++){
            printf("%c",uri[k]);
        }*/
        if(uri[i] == '.' && uri[i+1] == '.' && uri[i+2] == '/'){
            i += 3; //enlever prefixe
        }
        else if(uri[i] == '.' && uri[i+1] == '/'){
            i += 2; //enlever prefixe
        }
        else if(uri[i] == '/' && uri[i+1] == '.' && uri[i+2] == '.' && uri[i+3] == '/'){
            i += 3;
            //retirer le dernier /x de out:
            while(out[j-1] != '/'){
                //out[j] = '-';
                j--;
            }
            j--;

        }
        else if(uri[i] == '/' && uri[i+1] == '.' && uri[i+2] == '.'){
            i += 2;
            //retirer le dernier /x de out:
            while(out[j-1] != '/'){
                //out[j] = '-';
                j--;
            }
            j--;

        }
        else if(uri[i] == '/' && uri[i+1] == '.' && uri[i+2] == '/'){
            i += 2; //remplacer par '/'
        }
        else if(uri[i] == '/' && uri[i+1] == '.'){
            i += 1;
            uri[i] = '/'; //rempalcer par '/'
        }
        else if(uri[i] == '.'){
            i += 1; //retirer
        }
        else if(uri[i] == '.' && uri[i+1] == '.'){
            i += 2; //retirer
        }
        else{
            //placer /x dans out
            int debut = 1;
            
            while((uri[i] != '/' || debut == 1) && i != n){
                debut = 0;
                out[j] = uri[i];
                i++;
                j++; 
            }
        }
    }
    out[j] = '\0';
    return out;
}

void updateHeader(HTTPTable *codes, char *label, char *value) {
    for (int i = 0; i < codes->headersCount; i++) {
        if (strcmp(codes->headers[i].label, label) == 0) {
            codes->headers[i].value = malloc(strlen(value) + 1);
            strcpy(codes->headers[i].value, value);
        }
    }
}

int configFileMsgBody(char *name, HTTPTable *codes) {
    char *path = malloc(strlen(SERVER_ROOT) + strlen(name) + 1);
    strcpy(path, SERVER_ROOT);
    strcat(path, name);

    // Gérer le Content-type (à l'aide de `file`)
    char *command = malloc(512);
    sprintf(command, "file --brief --mime-type %s", path);
    FILE *fp = popen(command, "r");
    free(command);
    if (fp == NULL) { perror("popen"); return 500; }

    char *type = malloc(128);
    size_t n = fread(type, 1, 127, fp);
    type[n-1] = '\0';
    pclose(fp);

    updateHeader(codes, "Content-Type", type);

    FILE *file = fopen(path, "r");
    if (file == NULL) { 
        int code = configFileMsgBody("/404.html", codes);
        if (code != 1) { return code; }
        
        return 404;
    }
    else {
        fseek(file, 0, SEEK_END);
        long fsize = ftell(file);
        fseek(file, 0, SEEK_SET);

        char fsize_str[20];
        sprintf(fsize_str, "%ld", fsize);

        updateHeader(codes, "Content-Length", fsize_str);
        
        codes->filename = malloc(strlen(path) + 1);
        strcpy(codes->filename, path);
        free(path);

        if (strcmp(type, "application/x-httpd-php") == 0 || strcmp(type, "text/x-php") == 0) {
            codes->is_php = true;
            return 1;
            // on ne fait pas la suite car c'est pas le contenu du fichier qui nous intéresse
            // mais la partie "interprété" par PHP donc ça sera plus gros
        }
    }
    free(type);
    fclose(file);
    
    return 1;
}

int getRepCode(message req, HTTPTable *codes) {
    
    void *tree = getRootTree();
    int len;
    
    //HTTP Version
    _Token *versionNode = searchTree(tree, "HTTP_version");
    char *versionL = getElementValue(versionNode->node, &len);
    char majeur = versionL[5];
    char mineur = versionL[7];
    printf("%c, %c\n", majeur, mineur);

    if(mineur == '1' || mineur == '0'){
        codes->httpminor = mineur - '0';
    }
    else{
        codes->httpminor = '1';
    }
    
    if(majeur == '1' && mineur == '1'){
        updateHeader(codes, "Connection", "Keep-Alive");
    }

    if(!(majeur == '1' && (mineur == '1' || mineur == '0'))){
        int code  = configFileMsgBody("/505.html", codes);
        if (code != 1) { return code; }
        return 505;
    }

    //Methode
    _Token *methodNode = searchTree(tree, "method");
    char *methodL = getElementValue(methodNode->node, &len);
    char *method = malloc(len + 1);
    strncpy(method, methodL, len);
    method[len] = '\0';
    
    if (!(strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0 || strcmp(method, "HEAD") == 0)) { return 405; }
    else if (len > LEN_METHOD) { return 501; }
    else if (strcmp(method, "HEAD") == 0) { codes->is_head = true; }
    
    if ((strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) && (searchTree(tree,"message_body") != NULL)){ return 400; }
    free(method);

    _Token *uriNode = searchTree(tree, "request_target");
    char *uriL = getElementValue(uriNode->node, &len);
    char *uri = malloc(len + 1);
    strncpy(uri, uriL, len);
    uri[len] = '\0';
    if (len > LEN_URI) { return 414; }
    if (strcmp(uri, "/") == 0) {
        uri = realloc(uri, strlen("/index.html") + 1);
        strcpy(uri, "/index.html");
    }
    
    //percent-Encoding
    uri = percentEncoding(uri);
    uri = DotRemovalSegment(uri);

    int code = configFileMsgBody(uri, codes);
    if (code != 1) { return code; }
    
    free(uri);

    // Host
    _Token *HostNode = searchTree(tree, "Host");
    if (majeur == '1' && mineur == '1' && HostNode == NULL) { return 400; } // HTTP/1.1 sans Host
    if ((HostNode != NULL) && (HostNode->next != NULL)) { return 400; } // plusieurs Host
    if (HostNode != NULL) {
        char *hostL = getElementValue(HostNode->node, &len);
        char *host = malloc(len + 1);
        strncpy(host, hostL, len);
        host[len] = '\0';
        if(len > LEN_HOST){return 400;}

        // déterminer la nature du host (nom de domaine ou ip)
        int i=0;
        int point = 0;
        int d_point = 0;
        while(host[i]!='\0'){
            if(host[i]=='.'){point++;}
            else if(host[i]==':'){d_point++;}
            i++;
        }

        //if(point<2 | point>3){return 400;}

        if(point == 2 && d_point == 0){// nom de domaine sans port
            char txt1[63],txt2[63],txt3[63];

            sscanf(host, "%[^.].%[^.].%s", txt1, txt2, txt3);
            

            i=0;
            while (txt1[i] != '\0'){
                if(!((txt1[i]>=65 && txt1[i]<=90) | (txt1[i]>=97 && txt1[i]<=122) | txt1[i] == '-')){return 400;}
                i++;
            }
            if (i>LEN_HOST_TXT){return 400;}

            i=0;
            while (txt2[i] != '\0'){
                if(!((txt2[i]>=65 && txt2[i]<=90) | (txt2[i]>=97 && txt2[i]<=122) | txt2[i] == '-')){return 400;}
                i++;
            }
            if (i>LEN_HOST_TXT){return 400;}

            i=0;
            while (txt3[i] != '\0'){
                if(!((txt3[i]>=65 && txt3[i]<=90) | (txt3[i]>=97 && txt3[i]<=122) | txt3[i] == '-')){return 400;}
                i++;
            }
            if (i>LEN_HOST_TXT){return 400;}

        }

        if(point == 2 && d_point == 1){// nom de domaine avec port
            char txt1[63],txt2[63],txt3[63];
            int port;

            sscanf(host, "%[^.].%[^.].%[^:]:%d", txt1, txt2, txt3,&port);

            i=0;
            while (txt1[i] != '\0'){
                if(!((txt1[i]>=65 && txt1[i]<=90) | (txt1[i]>=97 && txt1[i]<=122) | txt1[i] == '-')){return 400;}
                i++;
            }
            if (i>LEN_HOST_TXT){return 400;}

            i=0;
            while (txt2[i] != '\0'){
                if(!((txt2[i]>=65 && txt2[i]<=90) | (txt2[i]>=97 && txt2[i]<=122) | txt2[i] == '-')){return 400;}
                i++;
            }
            if (i>LEN_HOST_TXT){return 400;}

            i=0;
            while (txt3[i] != '\0'){
                if(!((txt3[i]>=65 && txt3[i]<=90) | (txt3[i]>=97 && txt3[i]<=122) | txt3[i] == '-')){return 400;}
                i++;
            }
            if (i>LEN_HOST_TXT){return 400;}

            if (port!=8080){return 400;}

        }

        if (point == 3 && d_point == 0){// ip sans port
            int ip[4];
            sscanf(host, "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
            if((ip[0]>256|ip[0]<0) | (ip[1]>256|ip[1]<0) | (ip[2]>256|ip[2]<0) | (ip[3]>256|ip[3]<0)){return 400;}
        }

        if (point == 3 && d_point == 1){// ip avec port
            int ip_port[5];
            sscanf(host, "%d.%d.%d.%d:%d", &ip_port[0], &ip_port[1], &ip_port[2], &ip_port[3], &ip_port[4]);
            if((ip_port[0]>256|ip_port[0]<0) | (ip_port[1]>256|ip_port[1]<0) | (ip_port[2]>256|ip_port[2]<0) | (ip_port[3]>256|ip_port[3]<0) | ip_port[4] != 8080){return 400;} //ip invalide
        }
        
        
        free(host);
    }

    _Token *C_LengthNode = searchTree(tree, "Content_Length_header");
    _Token *T_EncodingNode = searchTree(tree, "Transfer_Encoding_header");
    _Token *Message_Body = searchTree(tree,"message_body");

    // Content-Length

    if(T_EncodingNode != NULL && C_LengthNode != NULL){return 400;} // Ne pas mettre s’il y a déjà Transfer-Encoding : 400

    if(C_LengthNode != NULL && C_LengthNode->next != NULL){return 400;} // plusieurs content-length

    if (C_LengthNode != NULL && C_LengthNode->next == NULL){ // si un seul Content-Length avec valeur invalide : 400
        char *CL_text = getElementValue(C_LengthNode->node, &len); // nous renvoie "Content-Length : xxxxx" et pas "xxxxxx"
        char c_length[len];

        sscanf(CL_text, "%*s %s", c_length);

        if(c_length[0]== '0'){return 400;} // on ne veut pas de 0XXXX
        
        int i=0;
        while(c_length[i] != '\0'){
            printf("c_length[%d] : %c\n",i,c_length[i]);
            if(!(c_length[i] >= '0' && c_length[i] <= '9')){return 400;} // valeur invalide (négative ou avec des caractères autres que des chiffres)
            i++;
        }
        /*
        int content_l_value = atoi(c_length);
        char *message_bodyL = getElementValue(Message_Body->node, &len);
        if(content_l_value != len){return 400;}// vérifier que c'est la taille du message body
        */
    }

    // Transfer-Encoding

    if (majeur == '1' && mineur == '1' && T_EncodingNode != NULL){ // Ne pas traiter si HTTP 1.0
        char *TE_text = getElementValue(T_EncodingNode->node, &len); // nous renvoie "Transfer-Encoding : xxxxx" et pas "xxxxxx"
        
        char transfer_encoding[len];
        sscanf(TE_text, "%*s %s", transfer_encoding);
        printf("te : -%s-\n",transfer_encoding);

        if(!(strcmp(transfer_encoding,"chunked")==0 | strcmp(transfer_encoding,"gzip")==0 | strcmp(transfer_encoding,"deflate")==0 | strcmp(transfer_encoding,"compress")==0 |strcmp(transfer_encoding,"identity")==0 )) {return 400;} // vérifier que la valeur du champ est bien prise en charge
        printf("ici1 : -%s-\n",TE_text);
        if(!(TE_text[len]=='\r' && TE_text[len+1]=='\n')){return 400;} //&& TE_text[len+2]=='\r' && TE_text[len+3]=='\n' : vérifier \r\n(\r\n) après la valeur du champ
        printf("ici2\n");    
    }

    // Message Body

    if (Message_Body != NULL && C_LengthNode == NULL){return 411;} // Si Message Body mais pas Content-Length : 411 Length Required


    // Accept-Encoding

    _Token *Accept_Encoding = searchTree(tree,"header_field");
    if(Accept_Encoding != NULL){
    
        char *ae_t = getElementValue(Accept_Encoding->node, &len);
        char *ae = malloc(16);
        strncpy(ae, ae_t, 15);
        ae[15] = '\0';

        
        while(Accept_Encoding->next != NULL && strcmp(ae,"Accept-Encoding") != 0){
            Accept_Encoding = Accept_Encoding->next;
            ae_t = getElementValue(Accept_Encoding->node, &len);
            ae = malloc(16);
            strncpy(ae, ae_t, 15);
            ae[15] = '\0';
        }
        

        if (strcmp(ae,"Accept-Encoding") == 0){
            char *AE_text = getElementValue(Accept_Encoding->node, &len);
            char accept_encoding[len];
            sscanf(AE_text, "%*s %s", accept_encoding);

            char *ae_value = strtok(accept_encoding, ", ");
            printf("OK1\n");
            while (ae_value != NULL) {
                if(strcmp(ae_value,"gzip")!=0 && strcmp(ae_value,"deflate")!=0 && strcmp(ae_value,"br")!=0 && strcmp(ae_value,"compress")!=0 && strcmp(ae_value,"identity")!=0){return 400;}
                ae_value = strtok(NULL, ", ");
            }
            printf("OK2\n");

        }
        free(ae);
    }
    // Accept

    _Token *Accept = searchTree(tree,"header_field");
    if(Accept != NULL){
        char *a_t = getElementValue(Accept->node, &len);
        char *a = malloc(8);
        strncpy(a, a_t,7);
        a[7] = '\0';

        
        while(Accept->next != NULL && strcmp(a,"Accept:") != 0){
            Accept = Accept->next;
            a_t = getElementValue(Accept->node, &len);
            a = malloc(8);
            strncpy(a, a_t, 7);
            a[7] = '\0';
        }
        

        if (strcmp(a,"Accept:") == 0){
            char *A_text = getElementValue(Accept->node, &len);
            char accept[len];
            sscanf(A_text, "%*s %s", accept);

            char *a_value = strtok(accept, ", ");
            
            while (a_value != NULL) {
                if(strcmp(a_value,"text/html")!=0 && strcmp(a_value,"text/css")!=0 && strcmp(a_value,"text/javascript")!=0 && strcmp(a_value,"application/json")!=0 && strcmp(a_value,"image/jpeg")!=0 && strcmp(a_value,"image/png")!=0 && strcmp(a_value,"application/pdf")!=0 && strcmp(a_value,"image/gif")!=0 && strcmp(a_value,"image/svg+xml")!=0 && strcmp(a_value,"image/tiff")!=0 && strcmp(a_value,"video/mp4")!=0){return 400;}
                a_value = strtok(NULL, ", ");
            }


        }
        free(a);
    }
    
    // Connection
    printf("debut connection\n");
    _Token *ConnectionNode = searchTree(tree, "connection_option");
    if(ConnectionNode != NULL){
        printf("connection header\n");
        char *ConnectionL = getElementValue(ConnectionNode->node, &len);
        char *connection = malloc(len +1);
        strncpy(connection, ConnectionL, len);
        connection[len] = '\0';

        if(strcmp(connection,"close") == 0 || strcmp(connection,"Close") == 0){
            //renvoyer close
            updateHeader(codes,"Connection","close");
            //et fermer la connection    
            //requestShutdownSocket(req.clientId);
        }
        else if (majeur == '1' && mineur == '0' && (strcmp(connection,"keep-alive") == 0 || strcmp(connection,"Keep-Alive") == 0)){
            updateHeader(codes, "Connection", "Keep-Alive");
        }
        free(connection);
    }
    else if (majeur == '1' && mineur == '0'){ // si 1.0 et pas de Connection header : fermer la connection
        printf("version 1.0\n");
        //renvoyer close
        updateHeader(codes,"Connection","close");
        //et fermer la connection    
        //requestShutdownSocket(req.clientId);
        printf("fermeture de la connexion !!!\n");
    }
    
    printf("sortie de getRepCode\n");
    return 200;
}

HttpReponse *convertFCGI_HTTP(FCGI_Header *reponse, HTTPTable *codes) {
    HttpReponse *rep = malloc(sizeof(HttpReponse));
    // Faire en fonction de ce qu'il y a dans FGCI_Header reponse
    // rep->code = 200;
    // rep->httpminor = 1;
    // rep->filename = NULL;
    // rep->is_head = false;
    // rep->headers = malloc(4 * sizeof(Header));
    // rep->headersCount = 4;

    // rep->headers[0].label = "Content-Type";
    // rep->headers[0].value = "text/html";
    // rep->headers[1].label = "Content-Length";
    // rep->headers[1].value = "0";
    // rep->headers[2].label = "Connection";
    // rep->headers[2].value = "Keep-Alive";
    // rep->headers[3].label = "Host";
    // rep->headers[3].value = "localhost:8080";

    return rep;
}

message *generateReponse(message req, int opt_code) {
    HTTPTable *codes = loadTable(); //initialisation de la table des codes possibles de retour

    int code;
    if (opt_code == -1) { code = getRepCode(req, codes); } //recherche du code à renvoyer
    else { code = opt_code; codes->httpminor = 0; }

    HttpReponse *rep;
    if (codes->is_php == 1) {
        printf("I'm your php\n");
        int sock = createConnexion();
        unsigned short requestId = 1;

        char srv_port_str[6];
        sprintf(srv_port_str, "%d", SERVER_PORT);

        send_begin_request(sock, requestId);
        printf("-> Sending params\n");
        send_params(sock, requestId, "SERVER_ADDR", SERVER_ADDR);
        send_params(sock, requestId, "SERVER_PORT", srv_port_str);
        send_params(sock, requestId, "DOCUMENT_ROOT", SERVER_ROOT);
        // SCRIPT_FILENAME = proxy:fcgi://127.0.0.1:9000//var/www/html/info.php
        send_params(sock, requestId, "SCRIPT_FILENAME", generateFileName(SERVER_ADDR, srv_port_str, codes->filename));
        //send_params(sock, requestId, "SCRIPT_FILENAME", codes->filename);
        send_params(sock, requestId, "SCRIPT_NAME", "/test.php"); // à modifier car par défaut pour les tests
        send_params(sock, requestId, "REQUEST_METHOD", "GET");

        send_empty_params(sock, requestId); // fin des paramètres
        send_stdin(sock, requestId, ""); // fin des données d'entrées

        FCGI_Header *reponse = receive_response(sock);
        rep = convertFCGI_HTTP(reponse, codes);

        close(sock);
    }
    else { rep = getTable(codes, code); }
    
    message *msg = createMsgFromReponse(*rep, req.clientId);

    freeTable(codes);
    return msg;
}
