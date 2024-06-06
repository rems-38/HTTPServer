#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "reponse.h"
#include "../api/api.h"
#include "config.h"

int connecte = 0;
int method_post = 0;

void initTable(HTTPTable *codes) {
    for (int i = 0; i < HTTP_CODE_MAX+1; i++) { codes->table[i] = NULL; }
    
    time_t now;
    time(&now);
    char* t = ctime(&now);
    int n = strlen(t);
    char* date = malloc(n);
    strncpy(date,t,n-1);
    date[n-1] = '\0';

    Header headers[] = {
        {"Date", date},
        {"Host", ""},
        {"Connection", ""},
        {"Content-Type", ""},
        {"Content-Length", ""}
    };
    free(date);
    int headersCount = sizeof(headers) / sizeof(headers[0]);

    codes->filename = NULL;
    codes->method = 0;
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
    for (int i = 0; i < HTTP_CODE_MAX+1; i++) {
        if (codes->table[i] != NULL) {
            free(codes->table[i]->info);
            free(codes->table[i]);
        }
    }
    if (codes->filename != NULL) {
        free(codes->filename);
    }
    for (int i = 0; i < codes->headersCount; i++) {
        free(codes->headers[i].label);
        free(codes->headers[i].value);
    }
    free(codes->headers);
}

void addTable(HTTPTable *codes, int code, char *info) {
    HttpCode *el = malloc(sizeof(HttpCode));
    
    el->code = code;
    el->info = malloc(strlen(info) + 1);
    strcpy(el->info, info);

    codes->table[code] = el;
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

    rep->code = codes->table[code];
    rep->httpminor = codes->httpminor;
    rep->filename = codes->filename;
    rep->method = codes->method;
    rep->headers = codes->headers;
    rep->headersCount = codes->headersCount;

    return rep;
}


message *createMsgFromReponse(HttpReponse rep, unsigned int clientId) {
    message *msg = malloc(sizeof(message));

    // Taille du fichier si existe
    FILE *fout = NULL;
    long fileSize = 0;
    if (rep.filename != NULL && rep.method != 2) {
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
    if (rep.method != 2) { bufSize += fileSize; }
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

    if (fout != NULL && rep.method != 2) {
        fread(msg->buf+len, fileSize, 1, fout);
        len += fileSize;
    }
    sprintf(msg->buf+len, "\r\n");
    
    msg->len = bufSize;
    msg->clientId = clientId;

    return msg;
}

message* createMsgFromReponsePHP(HttpReponse rep, unsigned int clientId, char* txtData){
    
    message *msg = malloc(sizeof(message));

   /*rep.headers[0].label = "Content-Type";
    rep.headers[0].value = "text/html";
    rep.headers[1].label = "Content-Length";
    rep.headers[1].value = "70000";
    rep.headersCount = 2;*/

    //recup code erreur (ou pas)
    //ajout du code dans rep 
    int code_out = ErrorInSTD_OUT(txtData);
    if(code_out != 200){
        HTTPTable *codes = loadTable(); //initialisation de la table des codes possibles de retour
        HttpReponse *rep_code_out = getTable(codes, code_out);
        rep.code->code = code_out;
        rep.code->info = rep_code_out->code->info;
        free(rep_code_out);
    }

    //recup header et les ajouter
    headers_from_STDOUT(txtData,rep);
   
    char *message_body = message_body_from_STD_OUT(txtData);
    int taille_msg_body = strlen(message_body);
    char *taille_msg_body_txt = malloc(10000000);
    sprintf(taille_msg_body_txt,"%d",taille_msg_body);
    updateHeaderHttpReponse(rep, "Content-Length", taille_msg_body_txt);
    free(taille_msg_body_txt);

    // Calcul de la taille nécessaire pour buf
    int bufSize = strlen("HTTP/1.x ") + 3 + strlen(" ") + strlen(rep.code->info) + strlen("\r\n"); // 3 : taille d'une code (200, 404, ...)
    for (int i = 0; i < rep.headersCount; i++) {
        if (!(strcmp(rep.headers[i].value, "") == 0)) {
            bufSize += strlen(rep.headers[i].label) + 2 + strlen(rep.headers[i].value) + strlen("\r\n"); // +2 pour le ": "
        }
    }
    if (rep.method != 2) { bufSize += strlen(message_body); } //ajout taille msg body
    bufSize += 2 * strlen("\r\n");
    //pas besoin ajout taille header normalement
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

    if (rep.method != 2) {//ajout msg body
        sprintf(msg->buf+len, "%s", message_body);
        len += strlen(message_body);

        free(message_body);
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

char *message_body_from_STD_OUT(char* STD_OUT_txt){


    int j = 0;// correspond au nombre de ligne à sauter d'affilée
    int i = 0;// correspond au nombre de caractères avant d'atteindre le message body

    while (j<2){ 
        j=0;
        while(STD_OUT_txt[i]!='\r'){i++;}
        while((STD_OUT_txt[i] == '\r') & (STD_OUT_txt[i+1] == '\n')){i=i+2;j++;}
    }

    int taille_msg_body = strlen(STD_OUT_txt) - i;
    char *message_body = malloc(taille_msg_body + 1);
    for(int j=0; j<taille_msg_body; j++){message_body[j]=STD_OUT_txt[i+j];}
    message_body[taille_msg_body] = '\0';

    return message_body;
}

void headers_from_STDOUT(char* STD_OUT_txt,HttpReponse rep){

    printf("STDOUT: %s\n",STD_OUT_txt);
    int j=0; // parcourir STD_OUT_txt
    int k=0; // taille label et value

    char *label = malloc(50);
    char *value = malloc(50);

    int n=0; //vérifier si on a qu'un seul CRLF, sinon fin de la boucle
    bool fin_headers = false;

    while (!fin_headers){
        k=0;

        while(STD_OUT_txt[j] != ':'){ //label
            label[k] = STD_OUT_txt[j];
            j++;
            k++;
        }
        label[k] = '\0';
        
        int MinToMaj=0;
        while(label[MinToMaj]!='-' && MinToMaj<strlen(label)){MinToMaj++;}
        MinToMaj++;
        if(MinToMaj != strlen(label)){
            label[MinToMaj] = label[MinToMaj] - ('a'-'A');
        }

        j=j+2;
        k=0;

        while(STD_OUT_txt[j] != '\r'){ //value
            value[k] = STD_OUT_txt[j];
            k++;
            j++;
        }
        value[k] = '\0';

        updateHeaderHttpReponse(rep,label,value);

        char *CRLF = malloc(3);
        CRLF[0] = STD_OUT_txt[j];
        CRLF[1] = STD_OUT_txt[j+1];
        CRLF[2] = '\0';

        while(strcmp(CRLF,"\r\n")==0){
            j = j+2;
            n++;
            CRLF[0] = STD_OUT_txt[j];
            CRLF[1] = STD_OUT_txt[j+1];
            CRLF[2] = '\0';
        }

        if(n>1){fin_headers = true;}
        n=0;
    }

    free(label);
    free(value);
}


int ErrorInSTD_OUT(char* STD_OUT_txt){ // retourne 200 si pas d'erreur, sinon retourne le numéro de l'erreur
    char *first_header = malloc(20);
    
    int i=0;

    while(STD_OUT_txt[i] != ':'){i++;}

    if(i>=6){
        
        int j=0;
        while(j < 6){
            first_header[j] = STD_OUT_txt[i-6+j];
            j++;
        }
        first_header[j] = '\0';
        
        if(strcmp(first_header,"Status") == 0){
            while(!((STD_OUT_txt[i]>='0' && STD_OUT_txt[i] <= '9') && (STD_OUT_txt[i+1]>='0' && STD_OUT_txt[i+1] <= '9') && (STD_OUT_txt[i+2]>='0' && STD_OUT_txt[i+2] <= '9'))){i++;}
            int error = (STD_OUT_txt[i]-'0')*100 + (STD_OUT_txt[i+1]-'0')*10 + STD_OUT_txt[i+2]-'0';
            return error;
        }
    }

    free(first_header);
    return 200;
}


char *HexaToChar(char *content){
    int content_length = strlen(content);
    char *result = malloc(content_length/2);
    char *hexa = malloc(3);

    int i = 0;
    int j = 0;

    while(i<content_length){
        while(content[i] == ' '){i++;}
        hexa[0] = content[i];
        i++;
        hexa[1] = content[i];
        i++;
        hexa[2] = '\0';
        char valeur = strtol(hexa,NULL,16);
        result[j] = valeur;
        j++;
    }
    return result;
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

void updateHeaderHttpReponse(HttpReponse rep, char *label, char *value) {
    for (int i = 0; i < rep.headersCount; i++) {
        if (strcmp(rep.headers[i].label, label) == 0) {
            rep.headers[i].value = malloc(strlen(value) + 1);
            strcpy(rep.headers[i].value, value);
        }
    }
}

void updateHeader(HTTPTable *codes, char *label, char *value) {
    for (int i = 0; i < codes->headersCount; i++) {
        if (strcmp(codes->headers[i].label, label) == 0) {
            codes->headers[i].value = malloc(strlen(value) + 1);
            strcpy(codes->headers[i].value, value);
        }
    }
}

int configFileMsgBody(char *name, HTTPTable *codes, char* host) {

    char* domaine = NULL;
    if(strcmp(host, "") != 0){
        domaine = strtok(host, ".");
    }
    else{
        domaine = "";
    }
    char* path = malloc(strlen(SERVER_ROOT) + strlen(name) + 1);
    if(strcmp(domaine, "www") == 0 || strcmp(domaine, "127") == 0 || strcmp(domaine, "") == 0 ){
        printf("www");
        path = realloc(path, strlen(SERVER_ROOT)+ sizeof("/www") + strlen(name) + 1);
        strcpy(path, SERVER_ROOT);
        strcat(path, "/www");
        strcat(path, name);
    }
    else{
        path = realloc(path, strlen(SERVER_ROOT)+ 1 + strlen(domaine) + strlen(name) + 1);
        strcpy(path, SERVER_ROOT);
        strcat(path, "/");
        strcat(path, domaine);
        strcat(path, name);
    }

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
        free(path);
        free(type);
        int code = configFileMsgBody("/404.html", codes, "");
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

int getRepCode(HTTPTable *codes) {
    
    void *tree = getRootTree();
    int len;
    
    //HTTP Version
    _Token *versionNode = searchTree(tree, "HTTP_version");
    char *versionL = getElementValue(versionNode->node, &len);
    char majeur = versionL[5];
    char mineur = versionL[7];

    if(mineur == '1' || mineur == '0'){
        codes->httpminor = mineur - '0';
    }
    else{
        codes->httpminor = '1';
    }
    
    if(majeur == '1' && mineur == '1'){
        updateHeader(codes, "Connection", "Keep-Alive");
        connecte = 0;
    }

    if(!(majeur == '1' && (mineur == '1' || mineur == '0'))){
        int code  = configFileMsgBody("/505.html", codes, "");
        if (code != 1) { return code; }
        return 505;
    }
    free(versionNode);

    //Methode
    _Token *methodNode = searchTree(tree, "method");
    char *methodL = getElementValue(methodNode->node, &len);
    char *method = malloc(len + 1);
    strncpy(method, methodL, len);
    method[len] = '\0';
    
    if (!(strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0 || strcmp(method, "HEAD") == 0)) { return 405; }
    else if (len > LEN_METHOD) { return 501; }
    else if (strcmp(method, "HEAD") == 0) { codes->method = 2; }
    else if (strcmp(method, "POST") == 0) { codes->method = 3; }
    
    if ((strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) && (searchTree(tree,"message_body") != NULL)){ return 400; }
    if((strcmp(method, "POST") == 0) && (searchTree(tree,"Content_Length_header") == NULL)){return 400;} //Post peut ne pas avoir de message body mais doit quand même avoir un content-length = 0
    free(method);
    free(methodNode);

    //URI
    printf("URI");
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
    char *uri2 = percentEncoding(uri);
    free(uri);
    char *uri3 = DotRemovalSegment(uri2);
    free(uri2);


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

        char *host_accepted[] = {"127.0.0.1","127.0.0.1:8080","www","test"}; // rajouter les sites au fur et à mesure
        int taille_host_accepted = 4; // à fixer à la main
        char *host_cpy = malloc(20);
        int i = 0;
        
        //printf("sizeof(host_accepted): %d",sizeof(host_accepted));

        while(i<taille_host_accepted){
            strncpy(host_cpy,host,len);
            
            if(len >= strlen(host_accepted[i])){
                int limit = strlen(host_accepted[i]);
                host_cpy[limit] = '\0';
                if(strcmp(host,host_accepted[i])==0){
                    break;
                }
            }
            i++;
        }
        if(i >= taille_host_accepted){
            printf("Site non référencé -> tableau dans champ Host ligne ~ 603\n");
            return 400;
        }

        int code = configFileMsgBody(uri3, codes, host);
        free(uri3);
        if (code != 1) { return code; }
        free(host);
    }
    else{
        printf("Pas Hostt");
        int code = configFileMsgBody(uri3, codes, "");
        free(uri3);
        if (code != 1) { return code; }
    }
    free(HostNode);

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

        if(c_length[0]== '0' && (c_length[1] >= '0' && c_length[1] <= '9')){return 400;} // on ne veut pas de 0XXXX
        
        int i=0;
        while(c_length[i] != '\0'){
            //printf("c_length[%d] : %c\n",i,c_length[i]);
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
        //printf("te : -%s-\n",transfer_encoding);

        if(!((strcmp(transfer_encoding,"chunked")==0) | (strcmp(transfer_encoding,"gzip")==0) | (strcmp(transfer_encoding,"deflate")==0) | (strcmp(transfer_encoding,"compress")==0) |(strcmp(transfer_encoding,"identity")==0) )) {return 400;} // vérifier que la valeur du champ est bien prise en charge
        //printf("ici1 : -%s-\n",TE_text);
        if(!(TE_text[len]=='\r' && TE_text[len+1]=='\n')){return 400;} //&& TE_text[len+2]=='\r' && TE_text[len+3]=='\n' : vérifier \r\n(\r\n) après la valeur du champ
        //printf("ici2\n");    
    }
    free(T_EncodingNode);

    // Message Body
    if (Message_Body != NULL && C_LengthNode == NULL){return 411;} // Si Message Body mais pas Content-Length : 411 Length Required
    free(C_LengthNode);
    free(Message_Body);


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
            while (ae_value != NULL) {
                if(strcmp(ae_value,"gzip")!=0 && strcmp(ae_value,"deflate")!=0 && strcmp(ae_value,"br")!=0 && strcmp(ae_value,"compress")!=0 && strcmp(ae_value,"identity")!=0){return 400;}
                ae_value = strtok(NULL, ", ");
            }

        }
        free(ae);
    }
    free(Accept_Encoding);

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
                //if(strcmp(a_value,"text/html")!=0 && strcmp(a_value,"text/css")!=0 && strcmp(a_value,"text/javascript")!=0 && strcmp(a_value,"application/json")!=0 && strcmp(a_value,"image/jpeg")!=0 && strcmp(a_value,"image/png")!=0 && strcmp(a_value,"application/pdf")!=0 && strcmp(a_value,"image/gif")!=0 && strcmp(a_value,"image/svg+xml")!=0 && strcmp(a_value,"image/tiff")!=0 && strcmp(a_value,"video/mp4")!=0){return 400;}
                a_value = strtok(NULL, ", ");
            }


        }
        free(a);
    }
    free(Accept);
    
    // Connection
    _Token *ConnectionNode = searchTree(tree, "connection_option");
    if(ConnectionNode != NULL){
        //printf("connection header\n");
        char *ConnectionL = getElementValue(ConnectionNode->node, &len);
        char *connection = malloc(len +1);
        strncpy(connection, ConnectionL, len);
        connection[len] = '\0';

        if(strcmp(connection,"close") == 0 || strcmp(connection,"Close") == 0){
            //renvoyer close
            updateHeader(codes,"Connection","close");
            //et fermer la connection    
            connecte = 1;
            //requestShutdownSocket(req.clientId);
        }
        else if (majeur == '1' && mineur == '0' && (strcmp(connection,"keep-alive") == 0 || strcmp(connection,"Keep-Alive") == 0)){
            updateHeader(codes, "Connection", "Keep-Alive");
            connecte = 0;
        }
        free(connection);
    }
    else if (majeur == '1' && mineur == '0'){ // si 1.0 et pas de Connection header : fermer la connection
        //printf("version 1.0\n");
        //renvoyer close
        updateHeader(codes,"Connection","close");
        connecte = 1;
        //et fermer la connection    
        //requestShutdownSocket(req.clientId);
        //printf("fermeture de la connexion !!!\n");
    }
    free(ConnectionNode);
    
    return 200;
}

void controlConnection(message *msg){
    if(connecte == 1){
        requestShutdownSocket(msg->clientId);
    }
}

void createSettingsParams(Header *settings, FCGI_NameValuePair11 *params, HTTPTable *codes) {
    Header sett[5] = {
        {"SCRIPT_NAME", getScriptName(codes->filename)},
        {"SCRIPT_FILENAME", getScriptFilename(codes->filename)},
        {"REQUEST_METHOD", ""},
        {"CONTENT_LENGTH", ""},
        {"message_body", ""}
    };

    if (codes->method == 1) { sett[2].value = "GET"; }
    else if (codes->method == 2) { sett[2].value = "HEAD"; }
    else if (codes->method == 3) {
        sett[2].value = "POST";

        int len;
        _Token *Message_Body = searchTree(getRootTree(),"message_body");
        char *msg = getElementValue(Message_Body->node, &len);

        char *len_str = malloc(7);
        sprintf(len_str, "%d", len);
        sett[3].value = len_str;
        sett[4].value = msg;
        free(Message_Body);
    }

    FCGI_NameValuePair11 *par = malloc(5 * sizeof(FCGI_NameValuePair11));
    for (int i = 0; i < 5; i++) {
        if (strcmp(settings[i].value, "") == 0) { break; }

        par[i].nameLengthB0 = strlen(settings[i].label);
        par[i].valueLengthB0 = strlen(settings[i].value);
        par[i].nameData = malloc(strlen(settings[i].label) + 1);
        par[i].valueData = malloc(strlen(settings[i].value) + 1);
        memcpy(par[i].nameData, settings[i].label, par[i].nameLengthB0);
        memcpy(par[i].valueData, settings[i].value, par[i].valueLengthB0);
    }

    settings = sett;
    params = par;

    free(par);
}


message *generateReponse(message req, int opt_code) {
    HTTPTable *codes = loadTable(); //initialisation de la table des codes possibles de retour

    int code;
    if (opt_code == -1) { code = getRepCode(codes); } //recherche du code à renvoyer
    else { code = opt_code; codes->httpminor = 0; }

    //printf("code de sortie : %d\n",code);

    HttpReponse *rep = getTable(codes, code);
    message *msg;
    if (codes->is_php) {
        int sock = createConnexion();
        unsigned short requestId = 1;

        char srv_port_str[6];
        sprintf(srv_port_str, "%d", SERVER_PORT);
        
        Header *settings;
        FCGI_NameValuePair11 *params;        
        createSettingsParams(settings, params, codes);

        send_begin_request(sock, requestId);
        send_params(sock, requestId, params);
        send_empty_params(sock, requestId); // fin des paramètres
        if(codes->method == 3){
            send_stdin(sock, requestId, settings[4].value);
        }
        send_stdin(sock, requestId, ""); // fin des données d'entrées

        char *HexData = receive_response(sock);

        msg = createMsgFromReponsePHP(*rep, req.clientId, HexData);

        free(HexData);
        free(settings);
        for (int i = 0; i < 3; i ++) {
            free(params[i].nameData);
            free(params[i].valueData);
        }
        free(params);
        close(sock);
    }
    else {
        msg = createMsgFromReponse(*rep, req.clientId);
    }

    free(rep);
    freeTable(codes);
    return msg;
}
