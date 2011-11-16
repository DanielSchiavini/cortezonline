// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// Para mais informações, veja LICENCE na pasta principal
// Traduzida pela equipe do Cronus Emulator

#include "../common/cbasetypes.h"
#include "../common/mmo.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/core.h"
#include "../common/showmsg.h"
#include "../common/malloc.h"
#include "../common/socket.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "atcommand.h"
#include "battle.h"
#include "chat.h"
#include "clif.h"
#include "chrif.h"
#include "duel.h"
#include "intif.h"
#include "itemdb.h"
#include "log.h"
#include "map.h"
#include "pc.h"
#include "status.h"
#include "skill.h"
#include "mob.h"
#include "npc.h"
#include "pet.h"
#include "homunculus.h"
#include "mercenary.h"
#include "elemental.h"
#include "party.h"
#include "guild.h"
#include "script.h"
#include "trade.h"
#include "unit.h"

#ifndef TXT_ONLY
#include "mail.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// variáveis externas
char atcommand_symbol = '@'; // primeiro char dos comandos
char charcommand_symbol = '#';
char* msg_table[MAX_MSG]; // Mensagens do server (0-499 reservados para comandos de GM, 500-999 reservados para outros)

// declarações locais
#define ACMD_FUNC(x) int atcommand_ ## x (const int fd, struct map_session_data* sd, const char* command, const char* message)

typedef struct AtCommandInfo
{
	const char* command;
	int level;
	int level2;
	AtCommandFunc func;
} AtCommandInfo;

static AtCommandInfo* get_atcommandinfo_byname(const char* name);
static AtCommandInfo* get_atcommandinfo_byfunc(const AtCommandFunc func);

ACMD_FUNC(commands);


/*=========================================
 * Variáveis genéricas
 *-----------------------------------------*/
char atcmd_output[CHAT_SIZE_MAX];
char atcmd_player_name[NAME_LENGTH];
char atcmd_temp[100];

// Compara função sorteando do maior para o menor
int hightolow_compare (const void * a, const void * b)
{
	return ( *(int*)b - *(int*)a );
}

// Compara função sorteando do menor para o maior
int lowtohigh_compare (const void * a, const void * b)
{
	return ( *(int*)a - *(int*)b );
}

//-----------------------------------------------------------
// Retorna a mensagem-string de número específico, por [Yor - eAthena]
//-----------------------------------------------------------
const char* msg_txt(int msg_number)
{
	if (msg_number >= 0 && msg_number < MAX_MSG &&
	    msg_table[msg_number] != NULL && msg_table[msg_number][0] != '\0')
		return msg_table[msg_number];

	return "??";
}

//-----------------------------------------------------------
// Retorna título do player (de msg_athena.conf) [Lupus - eAthena]
//-----------------------------------------------------------
static char* player_title_txt(int level)
{
	const char* format;
	format = (level >= battle_config.title_lvl8) ? msg_txt(332)
	       : (level >= battle_config.title_lvl7) ? msg_txt(331)
	       : (level >= battle_config.title_lvl6) ? msg_txt(330)
	       : (level >= battle_config.title_lvl5) ? msg_txt(329)
	       : (level >= battle_config.title_lvl4) ? msg_txt(328)
	       : (level >= battle_config.title_lvl3) ? msg_txt(327)
	       : (level >= battle_config.title_lvl2) ? msg_txt(326)
	       : (level >= battle_config.title_lvl1) ? msg_txt(325)
	       : "";
	sprintf(atcmd_temp, format, level);
	return atcmd_temp;
}


/*==========================================
 * Lê dados de mensagens
 *------------------------------------------*/
int msg_config_read(const char* cfgName)
{
	int msg_number;
	char line[1024], w1[1024], w2[1024];
	FILE *fp;
	static int called = 1;

	if ((fp = fopen(cfgName, "r")) == NULL) {
		ShowError("Arquivo de mensagens não encontrado: %s\n", cfgName);
		return 1;
	}

	if ((--called) == 0)
		memset(msg_table, 0, sizeof(msg_table[0]) * MAX_MSG);

	while(fgets(line, sizeof(line), fp))
	{
		if (line[0] == '/' && line[1] == '/')
			continue;
		if (sscanf(line, "%[^:]: %[^\r\n]", w1, w2) != 2)
			continue;

		if (strcmpi(w1, "import") == 0)
			msg_config_read(w2);
		else
		{
			msg_number = atoi(w1);
			if (msg_number >= 0 && msg_number < MAX_MSG)
			{
				if (msg_table[msg_number] != NULL)
					aFree(msg_table[msg_number]);
				msg_table[msg_number] = (char *)aMalloc((strlen(w2) + 1)*sizeof (char));
				strcpy(msg_table[msg_number],w2);
			}
		}
	}

	fclose(fp);

	return 0;
}

/*==========================================
 * Cleanup Message Data
 *------------------------------------------*/
void do_final_msg(void)
{
	int i;
	for (i = 0; i < MAX_MSG; i++)
		aFree(msg_table[i]);
}


/*==========================================
 * @send (usado para testar packets enviados pelo cliente)
 *------------------------------------------*/
ACMD_FUNC(send)
{
	int len=0,off,end,type;
	long num;
	(void)command; // not used

	// read message type as hex number (without the 0x)
	if(!message || !*message ||
			!((sscanf(message, "len %x", &type)==1 && (len=1))
			|| sscanf(message, "%x", &type)==1) )
	{
		clif_displaymessage(fd, "Uso:");
		clif_displaymessage(fd, "	@send len <número do packet>");
		clif_displaymessage(fd, "	@send <número do packet> {<value>}*");
		clif_displaymessage(fd, "	Valor: <type=B(default),W,L><número> or S<comprimento>\"<string>\"");
		return -1;
	}

#define PARSE_ERROR(error,p) \
	{\
		clif_displaymessage(fd, (error));\
		sprintf(atcmd_output, ">%s", (p));\
		clif_displaymessage(fd, atcmd_output);\
	}
//define PARSE_ERROR

#define CHECK_EOS(p) \
	if(*(p) == 0){\
		clif_displaymessage(fd, "Unexpected end of string");\
		return -1;\
	}
//define CHECK_EOS

#define SKIP_VALUE(p) \
	{\
		while(*(p) && !ISSPACE(*(p))) ++(p); /* non-space */\
		while(*(p) && ISSPACE(*(p)))  ++(p); /* space */\
	}
//define SKIP_VALUE

#define GET_VALUE(p,num) \
	{\
		if(sscanf((p), "x%lx", &(num)) < 1 && sscanf((p), "%ld ", &(num)) < 1){\
			PARSE_ERROR("Invalid number in:",(p));\
			return -1;\
		}\
	}
//define GET_VALUE

	if (type > 0 && type < MAX_PACKET_DB) {

		if(len)
		{// mostra o comprimento do packet
			sprintf(atcmd_output, "Packet 0x%x length: %d", type, packet_db[sd->packet_ver][type].len);
			clif_displaymessage(fd, atcmd_output);
			return 0;
		}

		len=packet_db[sd->packet_ver][type].len;
		off=2;
		if(len == 0)
		{// packet desconhecido - ERROR
			sprintf(atcmd_output, "Unknown packet: 0x%x", type);
			clif_displaymessage(fd, atcmd_output);
			return -1;
		} else if(len == -1)
		{// packet dinâmico
			len=SHRT_MAX-4; // maximum length
			off=4;
		}
		WFIFOHEAD(fd, len);
		WFIFOW(fd,0)=TOW(type);

		// analisar o conteúdo do packet
		SKIP_VALUE(message);
		while(*message != 0 && off < len){
			if(ISDIGIT(*message) || *message == '-' || *message == '+')
			{// padrão (byte)
				GET_VALUE(message,num);
				WFIFOB(fd,off)=TOB(num);
				++off;
			} else if(TOUPPER(*message) == 'B')
			{// byte
				++message;
				GET_VALUE(message,num);
				WFIFOB(fd,off)=TOB(num);
				++off;
			} else if(TOUPPER(*message) == 'W')
			{// palavra (2 bytes)
				++message;
				GET_VALUE(message,num);
				WFIFOW(fd,off)=TOW(num);
				off+=2;
			} else if(TOUPPER(*message) == 'L')
			{// palavra longa (4 bytes)
				++message;
				GET_VALUE(message,num);
				WFIFOL(fd,off)=TOL(num);
				off+=4;
			} else if(TOUPPER(*message) == 'S')
			{// string - espaços são válidos
				// get comprimento da string - num <= 0 significa um comprimento não definido(default)
				++message;
				if(*message == '"'){
					num=0;
				} else {
					GET_VALUE(message,num);
					while(*message != '"')
					{// encontra o inicio da string
						if(*message == 0 || ISSPACE(*message)){
							PARSE_ERROR("Not a string:",message);
							return -1;
						}
						++message;
					}
				}

				// analisa string
				++message;
				CHECK_EOS(message);
				end=(num<=0? 0: min(off+((int)num),len));
				for(; *message != '"' && (off < end || end == 0); ++off){
					if(*message == '\\'){
						++message;
						CHECK_EOS(message);
						switch(*message){
							case 'a': num=0x07; break; // Bell
							case 'b': num=0x08; break; // Backspace
							case 't': num=0x09; break; // Horizontal tab
							case 'n': num=0x0A; break; // Line feed
							case 'v': num=0x0B; break; // Vertical tab
							case 'f': num=0x0C; break; // Form feed
							case 'r': num=0x0D; break; // Carriage return
							case 'e': num=0x1B; break; // Escape
							default:  num=*message; break;
							case 'x': // Hexadecimal
							{
								++message;
								CHECK_EOS(message);
								if(!ISXDIGIT(*message)){
									PARSE_ERROR("Não é um dígito hexadecimal:",message);
									return -1;
								}
								num=(ISDIGIT(*message)?*message-'0':TOLOWER(*message)-'a'+10);
								if(ISXDIGIT(*message)){
									++message;
									CHECK_EOS(message);
									num<<=8;
									num+=(ISDIGIT(*message)?*message-'0':TOLOWER(*message)-'a'+10);
								}
								WFIFOB(fd,off)=TOB(num);
								++message;
								CHECK_EOS(message);
								continue;
							}
							case '0':
							case '1':
							case '2':
							case '3':
							case '4':
							case '5':
							case '6':
							case '7': // Octal
							{
								num=*message-'0'; // primeiro dígito octal
								++message;
								CHECK_EOS(message);
								if(ISDIGIT(*message) && *message < '8'){
									num<<=3;
									num+=*message-'0'; // segundo dígito octal
                                    ++message;
									CHECK_EOS(message);
									if(ISDIGIT(*message) && *message < '8'){
										num<<=3;
										num+=*message-'0'; // terceiro dígito octal
										++message;
										CHECK_EOS(message);
									}
								}
								WFIFOB(fd,off)=TOB(num);
								continue;
							}
						}
					} else
						num=*message;
					WFIFOB(fd,off)=TOB(num);
					++message;
					CHECK_EOS(message);
				}//for
				while(*message != '"')
				{// ignora caracteres extras
					++message;
					CHECK_EOS(message);
				}

				// termina a string
				if(off < end)
				{// preenche o resto com 0's
					memset(WFIFOP(fd,off),0,end-off);
					off=end;
				}
			} else
			{// desconhecido
				PARSE_ERROR("Tipo de valor desconhecido em:",message);
				return -1;
			}
			SKIP_VALUE(message);
		}

		if(packet_db[sd->packet_ver][type].len == -1)
		{// enviar packet dinâmico
			WFIFOW(fd,2)=TOW(off);
			WFIFOSET(fd,off);
		} else
		{// enviar packet estático
			if(off < len)
				memset(WFIFOP(fd,off),0,len-off);
			WFIFOSET(fd,len);
		}
	} else {
		clif_displaymessage(fd, msg_txt(259)); // Packet inválido
		return -1;
	}
	sprintf (atcmd_output, msg_txt(258), type, type); //enviado packet 0x%x (%d)
	clif_displaymessage(fd, atcmd_output);
	return 0;
#undef PARSE_ERROR
#undef CHECK_EOS
#undef SKIP_VALUE
#undef GET_VALUE
}

/*==========================================
 * @rura, @warp, @mapmove
 *------------------------------------------*/
ACMD_FUNC(mapmove)
{
	char map_name[MAP_NAME_LENGTH_EXT];
	unsigned short mapindex;
	short x = 0, y = 0;
	int m = -1;

	nullpo_retr(-1, sd);

	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message ||
		(sscanf(message, "%15s %hd %hd", map_name, &x, &y) < 3 &&
		 sscanf(message, "%15[^,],%hd,%hd", map_name, &x, &y) < 1))
	{
			clif_displaymessage(fd, "Por favor, entre com um mapa (uso: @warp/@rura/@mapmove <nomedomapa> <x> <y>).");
			return -1;
	}

	mapindex = mapindex_name2id(map_name);
	if (mapindex)
		m = map_mapindex2mapid(mapindex);
	
	if (!mapindex) { // m < 0 significa em server diferente! [Kevin]
		clif_displaymessage(fd, msg_txt(1)); // Mapa não encontrado.
		return -1;
	}

	if ((x || y) && map_getcell(m, x, y, CELL_CHKNOPASS))
  	{	//Isto é pra prevenir a chamada do pc_setpos ao mostrar um erro.
		clif_displaymessage(fd, msg_txt(2));
		if (!map_search_freecell(NULL, m, &x, &y, 10, 10, 1))
			x = y = 0; //Célula inválida, usa um lugar aleatório.
	}
	if (map[m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(247));
		return -1;
	}
	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(248));
		return -1;
	}
	if (pc_setpos(sd, mapindex, x, y, CLR_TELEPORT) != 0) {
		clif_displaymessage(fd, msg_txt(1)); // Mapa não encontrado
		return -1;
	}

	clif_displaymessage(fd, msg_txt(0)); // Teletransportado.
	return 0;
}

/*==========================================
 * Mostra onde um personagem está. Versão corrigida por Silent. [Skotlex]
 *------------------------------------------*/
ACMD_FUNC(where)
{
	struct map_session_data* pl_sd;

	nullpo_retr(-1, sd);
	memset(atcmd_player_name, '\0', sizeof atcmd_player_name);

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um personagem (uso: @where <nome do char>).");
		return -1;
	}

	pl_sd = map_nick2sd(atcmd_player_name);
	if( pl_sd == NULL
	||  strncmp(pl_sd->status.name,atcmd_player_name,NAME_LENGTH) != 0
	||  (battle_config.hide_GM_session && pc_isGM(sd) < pc_isGM(pl_sd) && !(battle_config.who_display_aid && pc_isGM(sd) >= battle_config.who_display_aid))
	) {
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}

	snprintf(atcmd_output, sizeof atcmd_output, "%s %s %d %d", pl_sd->status.name, mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(jumpto)
{
	struct map_session_data *pl_sd = NULL;

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um jogador (uso: @jumpto/@warpto/@goto <nome do player/id>).");
		return -1;
	}

	if((pl_sd=map_nick2sd((char *)message)) == NULL && (pl_sd=map_charid2sd(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado
		return -1;
	}
	
	if (pl_sd == sd)
	{
		clif_displaymessage(fd, "Mas você já está onde você está...");
		return -1;
	}
	
	if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd))
	{
		clif_displaymessage(fd, msg_txt(247));	// Você não está autorizado a se teletransportar para este mapa.
		return -1;
	}
	
	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd))
	{
		clif_displaymessage(fd, msg_txt(248));	// Você não está autorizado a se teletransportar a partir do mapa em que você está.
		return -1;
	}

	if( pc_isdead(sd) )
	{
		clif_displaymessage(fd, "Você não pode usar este comando enquanto morto.");
		return -1;
	}

	pc_setpos(sd, pl_sd->mapindex, pl_sd->bl.x, pl_sd->bl.y, CLR_TELEPORT);
	sprintf(atcmd_output, msg_txt(4), pl_sd->status.name); // Jumped to %s
 	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(jump)
{
	short x = 0, y = 0;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	sscanf(message, "%hd %hd", &x, &y);

	if (map[sd->bl.m].flag.noteleport && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(248));	// Você não está autorizado a se teletransportar a partir do mapa em que você está.
		return -1;
	}

	if( pc_isdead(sd) )
	{
		clif_displaymessage(fd, "Você não pode usar este comando enquanto morto");
		return -1;
	}

	if ((x || y) && map_getcell(sd->bl.m, x, y, CELL_CHKNOPASS))
  	{	//Isto é pra prevenir a chamada do pc_setpos ao mostrar um erro.
		clif_displaymessage(fd, msg_txt(2));
		if (!map_search_freecell(NULL, sd->bl.m, &x, &y, 10, 10, 1))
			x = y = 0; //Célula inválida, usa um lugar aleatório.
	}

	pc_setpos(sd, sd->mapindex, x, y, CLR_TELEPORT);
	sprintf(atcmd_output, msg_txt(5), sd->bl.x, sd->bl.y); // Jumped to %d %d
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * @who3 = nome do personagem, sua localização
 *------------------------------------------*/
ACMD_FUNC(who3)
{
	char temp0[100];
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int j, count;
	int pl_GM_level, GM_level;
	char match_text[100];
	char player_name[NAME_LENGTH];

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(match_text, '\0', sizeof(match_text));
	memset(player_name, '\0', sizeof(player_name));

	if (sscanf(message, "%99[^\n]", match_text) < 1)
		strcpy(match_text, "");
	for (j = 0; match_text[j]; j++)
		match_text[j] = TOLOWER(match_text[j]);

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if(!( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && pl_GM_level > GM_level ))
		{// você pode olhar apenas de seu nível para baixo
			memcpy(player_name, pl_sd->status.name, NAME_LENGTH);
			for (j = 0; player_name[j]; j++)
				player_name[j] = TOLOWER(player_name[j]);
			if (strstr(player_name, match_text) != NULL) { // procura sem case sensitive

				if (battle_config.who_display_aid > 0 && pc_isGM(sd) >= battle_config.who_display_aid) {
					sprintf(atcmd_output, "(CID:%d/AID:%d) ", pl_sd->status.char_id, pl_sd->status.account_id);
				} else {
					atcmd_output[0]=0;
				}
				//Nome do jogador
				sprintf(temp0, msg_txt(333), pl_sd->status.name);
				strcat(atcmd_output,temp0);
				//Título do jogador, se existir
				if (pl_GM_level > 0) {
					//sprintf(temp0, "(%s) ", player_title_txt(pl_GM_level) );
					sprintf(temp0, msg_txt(334), player_title_txt(pl_GM_level) );
					strcat(atcmd_output,temp0);
				}
				//Localização dos jogadores: mapa x y
				sprintf(temp0, msg_txt(338), mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
				strcat(atcmd_output,temp0);

				clif_displaymessage(fd, atcmd_output);
				count++;
			}
		}
	}
	mapit_free(iter);

	if (count == 0)
		clif_displaymessage(fd, msg_txt(28)); // Nenhum jogador encontrado
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(29)); // 1 jogador encontrado
	else {
		sprintf(atcmd_output, msg_txt(30), count); // %d jogadores encontrados
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Nome do jogador, BLevel, Job, 
 *------------------------------------------*/
ACMD_FUNC(who2)
{
	char temp0[100];
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int j, count;
	int pl_GM_level, GM_level;
	char match_text[100];
	char player_name[NAME_LENGTH];

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(match_text, '\0', sizeof(match_text));
	memset(player_name, '\0', sizeof(player_name));

	if (sscanf(message, "%99[^\n]", match_text) < 1)
		strcpy(match_text, "");
	for (j = 0; match_text[j]; j++)
		match_text[j] = TOLOWER(match_text[j]);

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if(!( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && (pl_GM_level > GM_level) ))
		{// Você pode olhar apenas de nível igual ou maior
			memcpy(player_name, pl_sd->status.name, NAME_LENGTH);
			for (j = 0; player_name[j]; j++)
				player_name[j] = TOLOWER(player_name[j]);
			if (strstr(player_name, match_text) != NULL) { // procura sem case sensitive
				//Nome dos jogadores
				//sprintf(atcmd_output, "Nome: %s ", pl_sd->status.name);
				sprintf(atcmd_output, msg_txt(333), pl_sd->status.name);
				//Título do jogador, se existir
				if (pl_GM_level > 0) {
					//sprintf(temp0, "(%s) ", player_title_txt(pl_GM_level) );
					sprintf(temp0, msg_txt(334), player_title_txt(pl_GM_level) );
					strcat(atcmd_output,temp0);
				}
				//Nível de base dos jogadores / nome da profissão (job)
				//sprintf(temp0, "| L:%d/%d | Job: %s", pl_sd->status.base_level, pl_sd->status.job_level, job_name(pl_sd->status.class_) );
				sprintf(temp0, msg_txt(337), pl_sd->status.base_level, pl_sd->status.job_level, job_name(pl_sd->status.class_) );
				strcat(atcmd_output,temp0);

				clif_displaymessage(fd, atcmd_output);
				count++;
			}
		}
	}
	mapit_free(iter);
	
	if (count == 0)
		clif_displaymessage(fd, msg_txt(28)); // Nenhum jogador encontrado.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(29)); // 1 jogador encontrado.
	else {
		sprintf(atcmd_output, msg_txt(30), count); // %d jogadores encontrados.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Nome do jogador, grupo dos jogadores / nome do clã
 *------------------------------------------*/
ACMD_FUNC(who)
{
	char temp0[100];
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int j, count;
	int pl_GM_level, GM_level;
	char match_text[100];
	char player_name[NAME_LENGTH];
	struct guild *g;
	struct party_data *p;

	nullpo_retr(-1, sd);

	memset(temp0, '\0', sizeof(temp0));
	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(match_text, '\0', sizeof(match_text));
	memset(player_name, '\0', sizeof(player_name));

	if (sscanf(message, "%99[^\n]", match_text) < 1)
		strcpy(match_text, "");
	for (j = 0; match_text[j]; j++)
		match_text[j] = TOLOWER(match_text[j]);

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if(!( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && pl_GM_level > GM_level ))
		{// Você pode olhar apenas de nível igual ou maior
			memcpy(player_name, pl_sd->status.name, NAME_LENGTH);
			for (j = 0; player_name[j]; j++)
				player_name[j] = TOLOWER(player_name[j]);
			if (strstr(player_name, match_text) != NULL) { // procurar sem case sensitive
				g = guild_search(pl_sd->status.guild_id);
				p = party_search(pl_sd->status.party_id);
				//Nome dos jogadores
				sprintf(atcmd_output, msg_txt(333), pl_sd->status.name);
				//Player title, if exists
				if (pl_GM_level > 0) {
					sprintf(temp0, msg_txt(334), player_title_txt(pl_GM_level) );
					strcat(atcmd_output,temp0);
				}
				//Grupo dos jogadores, se existir
				if (p != NULL) {
					//sprintf(temp0," | Party: '%s'", p->name);
					sprintf(temp0, msg_txt(335), p->party.name);
					strcat(atcmd_output,temp0);
				}
				//Clã dos jogadores, se existir
				if (g != NULL) {
					//sprintf(temp0," | Guild: '%s'", g->name);
					sprintf(temp0, msg_txt(336), g->name);
					strcat(atcmd_output,temp0);
				}
				clif_displaymessage(fd, atcmd_output);
				count++;
			}
		}
	}
	mapit_free(iter);

	if (count == 0)
		clif_displaymessage(fd, msg_txt(28)); // Nenhum jogador encontrado.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(29)); // 1 jogador encontrado.
	else {
		sprintf(atcmd_output, msg_txt(30), count); // %d jogadores encontrados.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(whomap3)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int count;
	int pl_GM_level, GM_level;
	int map_id;
	char map_name[MAP_NAME_LENGTH_EXT];

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message)
		map_id = sd->bl.m;
	else {
		sscanf(message, "%15s", map_name);
		if ((map_id = map_mapname2mapid(map_name)) < 0)
			map_id = sd->bl.m;
	}

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if( pl_sd->bl.m != map_id )
			continue;
		if( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && (pl_GM_level > GM_level) )
			continue;

		if (pl_GM_level > 0)
			sprintf(atcmd_output, "Nome: %s (GM:%d) | Localização: %s %d %d", pl_sd->status.name, pl_GM_level, mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
		else
			sprintf(atcmd_output, "Nome: %s | Localização: %s %d %d", pl_sd->status.name, mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
		clif_displaymessage(fd, atcmd_output);
		count++;
	}
	mapit_free(iter);

	if (count == 0)
		sprintf(atcmd_output, msg_txt(54), map[map_id].name); // Nenhum jogador encontrado no mapa '%s'.
	else if (count == 1)
		sprintf(atcmd_output, msg_txt(55), map[map_id].name); // Um jogador encontrado no mapa '%s'.
	else {
		sprintf(atcmd_output, msg_txt(56), count, map[map_id].name); // %d jogadores encontrados no mapa '%s'.
	}
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(whomap2)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int count;
	int pl_GM_level, GM_level;
	int map_id = 0;
	char map_name[MAP_NAME_LENGTH_EXT];

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message)
		map_id = sd->bl.m;
	else {
		sscanf(message, "%15s", map_name);
		if ((map_id = map_mapname2mapid(map_name)) < 0)
			map_id = sd->bl.m;
	}

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if( pl_sd->bl.m != map_id )
			continue;
		if( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && (pl_GM_level > GM_level) )
			continue;

		if (pl_GM_level > 0)
			sprintf(atcmd_output, "Nome: %s (GM:%d) | BLvl: %d | Job: %s (Lvl: %d)", pl_sd->status.name, pl_GM_level, pl_sd->status.base_level, job_name(pl_sd->status.class_), pl_sd->status.job_level);
		else
			sprintf(atcmd_output, "Nome: %s | BLvl: %d | Job: %s (Lvl: %d)", pl_sd->status.name, pl_sd->status.base_level, job_name(pl_sd->status.class_), pl_sd->status.job_level);
		clif_displaymessage(fd, atcmd_output);
		count++;
	}
	mapit_free(iter);

	if (count == 0)
		sprintf(atcmd_output, msg_txt(54), map[map_id].name); // Nenhum jogador encontrado no mapa '%s'.
	else if (count == 1)
		sprintf(atcmd_output, msg_txt(55), map[map_id].name); // Um jogador encontrado no mapa '%s'.
	else {
		sprintf(atcmd_output, msg_txt(56), count, map[map_id].name); // %d jogadores encontrados no mapa '%s'.
	}
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(whomap)
{
	char temp0[100];
	char temp1[100];
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	int count;
	int pl_GM_level, GM_level;
	int map_id = 0;
	char map_name[MAP_NAME_LENGTH_EXT];
	struct guild *g;
	struct party_data *p;

	nullpo_retr(-1, sd);

	memset(temp0, '\0', sizeof(temp0));
	memset(temp1, '\0', sizeof(temp1));
	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message)
		map_id = sd->bl.m;
	else {
		sscanf(message, "%15s", map_name);
		if ((map_id = map_mapname2mapid(map_name)) < 0)
			map_id = sd->bl.m;
	}

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if( pl_sd->bl.m != map_id )
			continue;
		if( (battle_config.hide_GM_session || (pl_sd->sc.option & OPTION_INVISIBLE)) && (pl_GM_level > GM_level) )
			continue;

		g = guild_search(pl_sd->status.guild_id);
		if (g == NULL)
			sprintf(temp1, "Nenhum");
		else
			sprintf(temp1, "%s", g->name);
		p = party_search(pl_sd->status.party_id);
		if (p == NULL)
			sprintf(temp0, "Nenhum");
		else
			sprintf(temp0, "%s", p->party.name);
		if (pl_GM_level > 0)
			sprintf(atcmd_output, "Nome: %s (GM:%d) | Party: '%s' | Clã: '%s'", pl_sd->status.name, pl_GM_level, temp0, temp1);
		else
			sprintf(atcmd_output, "Nome: %s | Party: '%s' | Clã: '%s'", pl_sd->status.name, temp0, temp1);
		clif_displaymessage(fd, atcmd_output);
		count++;
	}
	mapit_free(iter);

	if (count == 0)
		sprintf(atcmd_output, msg_txt(54), map[map_id].name); // Nenhum jogador encontrado no mapa '%s'.
	else if (count == 1)
		sprintf(atcmd_output, msg_txt(55), map[map_id].name); // Um jogador encontrado no mapa '%s'.
	else {
		sprintf(atcmd_output, msg_txt(56), count, map[map_id].name); // %d jogadores encontrados no mapa '%s'.
	}
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(whogm)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	int j, count;
	int pl_GM_level, GM_level;
	char match_text[CHAT_SIZE_MAX];
	char player_name[NAME_LENGTH];
	struct guild *g;
	struct party_data *p;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(match_text, '\0', sizeof(match_text));
	memset(player_name, '\0', sizeof(player_name));

	if (sscanf(message, "%199[^\n]", match_text) < 1)
		strcpy(match_text, "");
	for (j = 0; match_text[j]; j++)
		match_text[j] = TOLOWER(match_text[j]);

	count = 0;
	GM_level = pc_isGM(sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		pl_GM_level = pc_isGM(pl_sd);
		if (!pl_GM_level)
			continue;

		if (match_text[0])
		{
			memcpy(player_name, pl_sd->status.name, NAME_LENGTH);
			for (j = 0; player_name[j]; j++)
				player_name[j] = TOLOWER(player_name[j]);
		  	// procurar sem case sensitive
			if (strstr(player_name, match_text) == NULL)
				continue;
		}
		if (pl_GM_level > GM_level) {
			if (pl_sd->sc.option & OPTION_INVISIBLE)
				continue;
			sprintf(atcmd_output, "Nome: %s (GM)", pl_sd->status.name);
			clif_displaymessage(fd, atcmd_output);
			count++;
			continue;
		}

		sprintf(atcmd_output, "Nome: %s (GM:%d) | Localização: %s %d %d",
			pl_sd->status.name, pl_GM_level,
			mapindex_id2name(pl_sd->mapindex), pl_sd->bl.x, pl_sd->bl.y);
		clif_displaymessage(fd, atcmd_output);

		sprintf(atcmd_output, "       BLvl: %d | Job: %s (Lvl: %d)",
			pl_sd->status.base_level,
			job_name(pl_sd->status.class_), pl_sd->status.job_level);
		clif_displaymessage(fd, atcmd_output);
		
		p = party_search(pl_sd->status.party_id);
		g = guild_search(pl_sd->status.guild_id);
	
		sprintf(atcmd_output,"       Party: '%s' | Clã: '%s'",
			p?p->party.name:"Nenhum", g?g->name:"Nenhum");

		clif_displaymessage(fd, atcmd_output);
		count++;
	}
	mapit_free(iter);

	if (count == 0)
		clif_displaymessage(fd, msg_txt(150)); // Nenhum GM encontrado.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(151)); //Um GM encontrado.
	else {
		sprintf(atcmd_output, msg_txt(152), count); // %d GMs encontrados.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(save)
{
	nullpo_retr(-1, sd);

	pc_setsavepoint(sd, sd->mapindex, sd->bl.x, sd->bl.y);
	if (sd->status.pet_id > 0 && sd->pd)
		intif_save_petdata(sd->status.account_id, &sd->pd->pet);

	chrif_save(sd,0);
	
	clif_displaymessage(fd, msg_txt(6)); // O seu save point foi alterado.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(load)
{
	int m;

	nullpo_retr(-1, sd);

	m = map_mapindex2mapid(sd->status.save_point.map);
	if (m >= 0 && map[m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(249));	// Você não está autorizado a se teletransportar para seu mapa salvo.
		return -1;
	}
	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(248));	// Você não está autorizado a se teletransportar a partir do mapa onde você se encontra.
		return -1;
	}

	pc_setpos(sd, sd->status.save_point.map, sd->status.save_point.x, sd->status.save_point.y, CLR_OUTSIGHT);
	clif_displaymessage(fd, msg_txt(7)); // Teletransportando mapa o mapa salvo.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(speed)
{
	int speed;

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &speed) < 1) {
		sprintf(atcmd_output, "Por favor, entre um valor de velocidade (uso: @speed <%d-%d>).", MIN_WALK_SPEED, MAX_WALK_SPEED);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	sd->base_status.speed = cap_value(speed, MIN_WALK_SPEED, MAX_WALK_SPEED);
	status_calc_bl(&sd->bl, SCB_SPEED);
	clif_displaymessage(fd, msg_txt(8)); // Velocidade alterada.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(storage)
{
	nullpo_retr(-1, sd);
	
	if (sd->npc_id || sd->state.vending || sd->state.buyingstore || sd->state.trading || sd->state.storage_flag)
		return -1;
		
	 if (map[sd->bl.m].flag.nostorage && pc_isGM(sd) < 40) { 
		clif_displaymessage(sd->fd, "Você não pode abrir o armazém neste mapa."); 
		return -1;
	}

	if (storage_storageopen(sd) == 1)
	{	//Already open.
		clif_displaymessage(fd, msg_txt(250));
		return -1;
	}
	
	clif_displaymessage(fd, "Armazém aberto.");
	
	return 0;
}


/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(guildstorage)
{
	nullpo_retr(-1, sd);
	
	if (map[sd->bl.m].flag.noguildstorage && pc_isGM(sd) < 40) { 
		clif_displaymessage(sd->fd, "Você não pode abrir o armazém do clã neste mapa."); 
		return -1;
	}

	if (!sd->status.guild_id) {
		clif_displaymessage(fd, msg_txt(252));
		return -1;
	}

	if (sd->npc_id || sd->state.vending || sd->state.buyingstore || sd->state.trading)
		return -1;

	if (sd->state.storage_flag == 1) {
		clif_displaymessage(fd, msg_txt(250));
		return -1;
	}

	if (sd->state.storage_flag == 2) {
		clif_displaymessage(fd, msg_txt(251));
		return -1;
	}

	storage_guild_storageopen(sd);
	clif_displaymessage(fd, "Armazém do clã aberto.");
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(option)
{
	int param1 = 0, param2 = 0, param3 = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %d %d", &param1, &param2, &param3) < 1 || param1 < 0 || param2 < 0 || param3 < 0) {
		clif_displaymessage(fd, "Por favor, entre com pelo menos uma opção (uso: @option <param1:0+> <param2:0+> <param3:0+>).");
		return -1;
	}

	sd->sc.opt1 = param1;
	sd->sc.opt2 = param2;
	pc_setoption(sd, param3);
	
	clif_displaymessage(fd, msg_txt(9)); // Opções alteradas.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(hide)
{
	nullpo_retr(-1, sd);
	if (sd->sc.option & OPTION_INVISIBLE) {
		sd->sc.option &= ~OPTION_INVISIBLE;
		if (sd->disguise)
			status_set_viewdata(&sd->bl, sd->disguise);
		else
			status_set_viewdata(&sd->bl, sd->status.class_);
		clif_displaymessage(fd, msg_txt(10)); // Invisibilidade: Off
	} else {
		sd->sc.option |= OPTION_INVISIBLE;
		sd->vd.class_ = INVISIBLE_CLASS;
		clif_displaymessage(fd, msg_txt(11)); // Invisibilidade: On
	}
	clif_changeoption(&sd->bl);

	return 0;
}

/*==========================================
 * Muda a classe de um personagem
 *------------------------------------------*/
ACMD_FUNC(jobchange)
{
	//FIXME: redundância, código potencialmente errado, precisa usar job_name() ou similar ao invés de hardcoding a tabela [ultramage]
	int job = 0, upper = 0, option = 0;
	nullpo_retr(-1, sd);

	if( !message || !*message || sscanf(message, "%d %d", &job, &upper) < 1 )
	{
		int i, found = 0;
		const struct { char name[25]; int id; } jobs[] = {
			{ "aprendiz",		0 },
			{ "espadachim",	1 },
			{ "mago",		2 },
			{ "maga",		2 },
			{ "arqueiro",		3 },
			{ "arqueira",		3 },
			{ "noviço",	4 },
			{ "noviça",	4 },
			{ "mercador",	5 },
			{ "mercadora",	5 },
			{ "gatuno",		6 },
			{ "gatuna",		6 },
			{ "cavaleiro",		7 },
			{ "cavaleira",		7 },
			{ "sacerdote",		8 },
			{ "sacerdotiza",	8 },
			{ "bruxo",		9 },
			{ "bruxa",		9 },
			{ "ferreiro",	10 },
			{ "ferreira",	10 },
			{ "caçador",		11 },
			{ "caçadora",		11 },
			{ "mercenário",	12 },
			{ "mercenária",	12 },
			{ "templário",	14 },
			{ "templária",	14 },
			{ "monge",		15 },
			{ "sábio",		16 },
			{ "sábia",		16 },
			{ "arruaceiro",		17 },
			{ "arruaceira",		17 },
			{ "alquimista",	18 },
			{ "bardo",		19 },
			{ "odalisca",		20 },
			{ "super aprendiz",	23 },
			{ "justiceiro",	24 },
			{ "justiceira",	24 },
			{ "ninja",	25 },
			{ "aprendiz t.",	4001 },
			{ "espadachim t.",	4002 },
			{ "mago t",		4003 },
			{ "maga t.",		4003 },
			{ "arqueiro t.",	4004 },
			{ "arqueira t.",	4004 },
			{ "noviço t.",	4005 },
			{ "noviça t.",	4005 },
			{ "mercador t.",	4006 },
			{ "mercadora t.",	4006 },
			{ "gatuno t.",		4007 },
			{ "gatuna t.",		4007 },
			{ "lorde",	4008 },
			{ "lady",	4008 },
			{ "sumo-sacerdote",	4009 },
			{ "sumo-sacerdotiza",	4009 },
			{ "arquimago",	4010 },
			{ "arquimaga",	4010 },
			{ "mestre-ferreiro",		4011 },
			{ "mestre-ferreira",		4011 },
			{ "atirador de elite",		4012 },
			{ "atiradora de elite",		4012 },
			{ "algoz",	4013 },
			{ "paladino",	4015 },
			{ "paladina",	4015 },
			{ "mestre",	4016 },
			{ "professor",	4017 },
			{ "professora",	4017 },
			{ "desordeiro",	4018 },
			{ "desordeira",	4018 },
			{ "criador",	4019 },
			{ "criadora",	4019 },
			{ "menestrel",		4020 },
			{ "cigana",		4021 },
			{ "mini aprendiz",	4023 },
			{ "mini espadachim",	4024 },
			{ "mini mago",		4025 },
			{ "mini maga",		4025 },
			{ "mini arqueiro",	4026 },
			{ "mini arqueira",	4026 },
			{ "mini noviço",	4027 },
			{ "mini noviça",	4027 },
			{ "mini mercador",	4028 },
			{ "mini mercadora",	4028 },
			{ "mini gatuno",		4029 },
			{ "mini gatuna",		4029 },
			{ "mini cavaleiro",	4030 },
			{ "mini cavaleira",	4030 },
			{ "mini sacerdote",	4031 },
			{ "mini sacerdotiza",	4031 },
			{ "mini bruxo",	4032 },
			{ "mini bruxa",	4032 },
			{ "mini ferreiro",4033 },
			{ "mini ferreira",4033 },
			{ "mini caçador",	4034 },
			{ "mini caçadora",	4034 },
			{ "mini mercenário",	4035 },
			{ "mini mercenária",	4035 },
			{ "mini templário",	4037 },
			{ "mini templária",	4037 },
			{ "mini monge",		4038 },
			{ "mini sábio",		4039 },
			{ "mini sábia",		4039 },
			{ "mini arruaceiro",		4040 },
			{ "mini arruaceira",		4040 },
			{ "mini alquimista",	4041 },
			{ "mini bardo",		4042 },
			{ "mini odalisca",	4043 },
			{ "mini super aprendiz",		4045 },
			{ "taekwon",		4046 },
			{ "mestre taekwon",	4047 },
			{ "espiritualista",	4049 },
			{ "gangsi",		4050 },
			{ "bongun",		4050 },
			{ "munak",		4050 },
			{ "death knight",	4051 },
			{ "dark collector",	4052 },
			{ "cavaleiro runico",	4054 },
			{ "arcano",		4055 },
			{ "sentinela",		4056 },
			{ "arcebispo",	4057 },
			{ "mecanico",		4058 },
			{ "sicario",		4059 },
			{ "cavaleiro runico t",	4060 },
			{ "arcano t",		4061 },
			{ "sentinela t",		4062 },
			{ "arcebispo t",	4063 },
			{ "mecanico t",		4064 },
			{ "sicario t",	4065 },
			{ "guardiao real",	4066 },
			{ "feiticeiro",		4067 },
			{ "trovador",		4068 },
			{ "musa",		4069 },
			{ "shura",		4070 },
			{ "bioquimico",		4071 },
			{ "renegado",	4072 },
			{ "guardial real t",	4073 },
			{ "feiticeiro t",		4074 },
			{ "trovador t",		4075 },
			{ "musa t",		4076 },
			{ "shura t",		4077 },
			{ "bioquimico t",		4078 },
			{ "renegado t",	4079 },
			{ "mini cav runico",		4096 },
			{ "mini arcano",	4097 },
			{ "mini sentinela",	4098 },
			{ "mini arcebispo",	4099 },
			{ "mini mecanico",	4100 },
			{ "mini sicario",		4101 },
			{ "mini guardiao real",		4102 },
			{ "mini feiticeiro",	4103 },
			{ "mini trovador",	4104 },
			{ "mini musa",	4105 },
			{ "mini shura",		4106 },
			{ "mini bioquimico",	4107 },
			{ "mini renegado",	4108 },
			{ "super aprendiz e",	4190 },
			{ "super bebe e",	4191 },
		};

		for (i=0; i < ARRAYLENGTH(jobs); i++)
		{
			if( !strncmpi(message, jobs[i].name, 16) )
			{
				job = jobs[i].id;
				upper = 0;
				found = 1;
				break;
			}
		}

		if( !found )
		{
			goto l_job_displaymes;
			return -1;
		}
	}

	switch( job )
	{
 		case   13: case   21: case 4014: case 4022: case 4036:
		case 4044: case 4080: case 4081: case 4082: case 4083:
		case 4084: case 4085: case 4086: case 4087: case 4109:
		case 4110: case 4111: case 4112:
			return 0; // Recusa diretamente a transformação em jobs falsos
	}

	if( pcdb_checkid(job) )
	{
		if( !pc_jobchange(sd, job, upper) )
		{
			if( pc_isriding(sd,OPTION_RIDING) || pc_isriding(sd,OPTION_RIDING_DRAGON) || pc_isriding(sd,OPTION_RIDING_WUG) || pc_isriding(sd,OPTION_MADO) )
				pc_setoption(sd, sd->sc.option & ~option);
			clif_displaymessage(fd, msg_txt(12)); // Sua classe foi alterada.
			return 0;
		}
		else
		{
			clif_displaymessage(fd, msg_txt(155)); // Você não pode alterar sua classe.
			return -1;
		}
	}
	else
	{
		goto l_job_displaymes;
		return -1;
	}
	
	l_job_displaymes:
		clif_displaymessage(fd, "Por favor, entre com o ID de uma classe (uso: @job/@jobchange <nome da classe/ID>).");
		clif_displaymessage(fd, "[======= Aprendiz / Classes 1 ==============]");
		clif_displaymessage(fd, "0 Aprendiz            1 Espadachim          2 Mago              3 Arqueiro");
		clif_displaymessage(fd, "4 Noviço              5 Mercador            6 Gatuno");
		clif_displaymessage(fd, "[======= Classes 2 =========================]");
		clif_displaymessage(fd, " 7 Cavaleiro           8 Sacerdote          9 Bruxo            10 Ferreiro");
		clif_displaymessage(fd, "11 Caçador            12 Mercenário        14 Templário        15 Monge");
		clif_displaymessage(fd, "16 Sábio              17 Arruaceiro        18 Alquemista       19 Bardo");
		clif_displaymessage(fd, "20 Odalisca");
		clif_displaymessage(fd, "[======= Transclasses 1 ====================]");
		clif_displaymessage(fd, "4001 Aprendiz T.    4002 Espadachim T.   4003 Mago T.        4004 Arqueiro T.");
		clif_displaymessage(fd, "4005 Noviço T.      4006 Mercador T.     4007 Gatuno T.");
		clif_displaymessage(fd, "[======= Transclasses 2 ====================]");
		clif_displaymessage(fd, "4008 Lorde          4009 Sumo Sacerdote  4010 Arquimago      4011 Mestre-Ferreiro");
		clif_displaymessage(fd, "4012 Atirador       4013 Algoz           4015 Paladino       4016 Mestre");
		clif_displaymessage(fd, "4017 Professor      4018 Desordeiro      4019 Criador        4020 Menestrel");
		clif_displaymessage(fd, "4021 Cigana");
		clif_displaymessage(fd, "[======= Classes 3 (Normal para 3rd) =======]");
		clif_displaymessage(fd, "4054 Cavaleiro Rúnico  4055 Arcano           4056 Sentinela        4057 Arcebispo");
		clif_displaymessage(fd, "4058 Mecânico          4059 Sicário          4066 Guardião Real    4067 Arcano");
		clif_displaymessage(fd, "4068 Trovador          4069 Musa             4070 Shura            4071 Bioquímico");
		clif_displaymessage(fd, "4072 Renegado");
		clif_displaymessage(fd, "[======= Classes 3 (Trans. para 3rd) =======]");
		clif_displaymessage(fd, "4060 Cavaleiro Rúnico  4061 Arcano           4062 Sentinela        4063 Arcebispo");
		clif_displaymessage(fd, "4064 Mecânico          4065 Sicário          4073 Guardião Real    4074 Sorcerer");
		clif_displaymessage(fd, "4075 Trovador          4076 Musa             4077 Shura            4078 Bioquímico");
		clif_displaymessage(fd, "4079 Renegado");
		clif_displaymessage(fd, "[===== Classes Expandidas ==================]");
		clif_displaymessage(fd, "   23 Super Aprendiz        24 Justiceiro            25 Ninja   4046 Taekwon");
		clif_displaymessage(fd, "4047 Mestre Taekwon       4049 Espiritualista      4050 Gangsi  4051 Death Knight");
		clif_displaymessage(fd, "4052 Dark Collector       4190 Super Aprendiz Expandido         4191 Super Bebê Expandido");
		clif_displaymessage(fd, "[===== Classes 1 e 2 Mini ==================]");
		clif_displaymessage(fd, "4023 Mini Aprendiz    4024 Mini Espadachim  4025 Mini Mago      4026 Mini Arqueiro");
		clif_displaymessage(fd, "4027 Mini Noviço      4028 Mini Mercador    4029 Mini Gatuno    4030 Mini Cavaleiro");
		clif_displaymessage(fd, "4031 Mini Sacerdote   4032 Mini Bruxo       4033 Mini Ferreiro  4034 Mini Caçador");
		clif_displaymessage(fd, "4035 Mini Mercenário  4037 Mini Templário   4038 Mini Monge     4039 Mini Sábio");
		clif_displaymessage(fd, "4040 Mini Arruaceiro  4041 Mini Alquimista  4042 Mini Bardo     4043 Mini Odalisca");
		clif_displaymessage(fd, "4045 Super Mini");
		clif_displaymessage(fd, "[===== Classes 3 Mini ======================]");
		clif_displaymessage(fd, "4096 Mini Cavaleiro Rúnico   4097 Mini Arcano      4098 Mini Sentinela");
		clif_displaymessage(fd, "4099 Mini Arcebispo          4100 Mini Mecânico    4101 Mini Sicário");
		clif_displaymessage(fd, "4102 Mini Guardião Real      4103 Mini Arcano      4104 Mini Trovador");
		clif_displaymessage(fd, "4105 Mini Musa               4106 Mini Shura       4107 Mini Bioquímico");
		clif_displaymessage(fd, "4108 Mini Renegado");
		clif_displaymessage(fd, "[===== Eventos e Outros ====================]");
		clif_displaymessage(fd, "22 Casamento     26 Natal");
		clif_displaymessage(fd, "27 Verão         4048 Mestre Taekwon (União)");
		clif_displaymessage(fd, "[===== Montaria (Não permitido) ============]");
		clif_displaymessage(fd, "[-- PecoPeco --]");
		clif_displaymessage(fd, "  13 Cavaleiro     21 Templário        4014 Lorde");
		clif_displaymessage(fd, "4022 Paladino    4036 Mini Cavaleiro   4044 Mini Templário");
		clif_displaymessage(fd, "[-- Dragão ----]");
		clif_displaymessage(fd, "4080 Cavaleiro Rúnico   4081 Cavaleiro Rúnico T.");
		clif_displaymessage(fd, "[-- Grifo -----]");
		clif_displaymessage(fd, "4082 Guardião Real      4083 Guardião Real T.");
		clif_displaymessage(fd, "[-- Warg ------]");
		clif_displaymessage(fd, "4084 Sentinela          4085 Sentinela T.");
		clif_displaymessage(fd, "[-- Mado ------]");
		clif_displaymessage(fd, "4086 Mecânico           4087 Mecânico T.");

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(die)
{
	nullpo_retr(-1, sd);
	clif_specialeffect(&sd->bl,450,SELF);
	status_kill(&sd->bl);
	clif_displaymessage(fd, msg_txt(13)); // Que pena! Você morreu.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(kill)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um jogador (uso: @kill <nome/id>).");
		return -1;
	}

	if((pl_sd=map_nick2sd((char *)message)) == NULL && (pl_sd=map_charid2sd(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado
		return -1;
	}
	
	if (pc_isGM(sd) < pc_isGM(pl_sd))
	{ // você pode matar apenas de seu nível para baixo
		clif_displaymessage(fd, msg_txt(81)); // Seu nível de GM não te autoriza a fazer esta ação neste jogador.
		return -1;
	}
	
	status_kill(&pl_sd->bl);
	clif_displaymessage(pl_sd->fd, msg_txt(13)); // Que pena! Você morreu.
	if (fd != pl_sd->fd)
		clif_displaymessage(fd, msg_txt(14)); // Personagem morto.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(alive)
{
	nullpo_retr(-1, sd);
	if (!status_revive(&sd->bl, 100, 100))
	{
		clif_displaymessage(fd, "Você não está morto.");
		return -1;
	}
	clif_skill_nodamage(&sd->bl,&sd->bl,ALL_RESURRECTION,4,1);
	clif_displaymessage(fd, msg_txt(16)); // Você foi ressuscitado! É um milagre !
	return 0;
}

/*==========================================
 * +kamic [LuzZza]
 *------------------------------------------*/
ACMD_FUNC(kami)
{
	unsigned long color=0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if(*(command + 5) != 'c' && *(command + 5) != 'C') {

		if (!message || !*message) {
			clif_displaymessage(fd, "Por favor, entre com uma mensagem (uso: @kami <mensagem>).");
			return -1;
		}

		sscanf(message, "%199[^\n]", atcmd_output);
		intif_broadcast(atcmd_output, strlen(atcmd_output) + 1, (*(command + 5) == 'b' || *(command + 5) == 'B') ? 0x10 : 0);
	
	} else {
	
		if(!message || !*message || (sscanf(message, "%lx %199[^\n]", &color, atcmd_output) < 2)) {
			clif_displaymessage(fd, "Por favor, entre com a cor e com a mensagem (uso: @kamic <cor> <mensagem>).");
			return -1;
		}
	
		if(color > 0xFFFFFF) {
			clif_displaymessage(fd, "Cor inválida");
			return -1;
		}
	
		intif_broadcast2(atcmd_output, strlen(atcmd_output) + 1, color, 0x190, 12, 0, 0);
	}
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(heal)
{
	int hp = 0, sp = 0; // [Valaris] thanks to fov
	nullpo_retr(-1, sd);

	sscanf(message, "%d %d", &hp, &sp);

	// algumas checagens de overflow
	if( hp == INT_MIN ) hp++;
	if( sp == INT_MIN ) sp++;

	if ( hp == 0 && sp == 0 ) {
		if (!status_percent_heal(&sd->bl, 100, 100))
			clif_displaymessage(fd, msg_txt(157)); // HP e SP já foram recuperados.
		else
			clif_displaymessage(fd, msg_txt(17)); // HP, SP recuperados.
		return 0;
	}

	if ( hp > 0 && sp >= 0 ) {
		if(!status_heal(&sd->bl, hp, sp, 0))
			clif_displaymessage(fd, msg_txt(157)); // HP and SP já estão com um bom valor.
		else
			clif_displaymessage(fd, msg_txt(17)); // HP e SP recuperados.
		return 0;
	}

	if ( hp < 0 && sp <= 0 ) {
		status_damage(NULL, &sd->bl, -hp, -sp, 0, 0);
		clif_damage(&sd->bl,&sd->bl, gettick(), 0, 0, -hp, 0, 4, 0);
		clif_displaymessage(fd, msg_txt(156)); // HP e/ou SP alterado.
		return 0;
	}

	//Sinais em oposição.
	if ( hp ) {
		if (hp > 0)
			status_heal(&sd->bl, hp, 0, 0);
		else {
			status_damage(NULL, &sd->bl, -hp, 0, 0, 0);
			clif_damage(&sd->bl,&sd->bl, gettick(), 0, 0, -hp, 0, 4, 0);
		}
	}

	if ( sp ) {
		if (sp > 0)
			status_heal(&sd->bl, 0, sp, 0);
		else
			status_damage(NULL, &sd->bl, 0, -sp, 0, 0);
	}

	clif_displaymessage(fd, msg_txt(156)); // HP ou/e SP modificados.
	return 0;
}

/*==========================================
 * comando @item (uso: @item <nome/id_do_item> <quantidade>) (modificado por [Yor] para pet_egg)
 *------------------------------------------*/
ACMD_FUNC(item)
{
	char item_name[100];
	int number = 0, item_id, flag;
	struct item item_tmp;
	struct item_data *item_data;
	int get_count, i;
	nullpo_retr(-1, sd);

	memset(item_name, '\0', sizeof(item_name));

	if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d", item_name, &number) < 1 &&
		sscanf(message, "%99s %d", item_name, &number) < 1
	)) {
		clif_displaymessage(fd, "Por favor, entre com o nome/id do item (uso: @item <nome/ID do item> [quantidade]).");
		return -1;
	}

	if (number <= 0)
		number = 1;

	if ((item_data = itemdb_searchname(item_name)) == NULL &&
	    (item_data = itemdb_exists(atoi(item_name))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(19)); // Nome/ID inválido.
		return -1;
	}

	item_id = item_data->nameid;
	get_count = number;
	//Checa se é estocável.
	if (!itemdb_isstackable2(item_data))
		get_count = 1;

	for (i = 0; i < number; i += get_count) {
		// se não é ovo de pets
		if (!pet_create_egg(sd, item_id)) {
			memset(&item_tmp, 0, sizeof(item_tmp));
			item_tmp.nameid = item_id;
			item_tmp.identify = 1;

			if ((flag = pc_additem(sd, &item_tmp, get_count)))
				clif_additem(sd, 0, 0, flag);
		}
	}

	//Logs (A)dmins items [Lupus]
	if(log_config.enable_logs&0x400)
		log_pick_pc(sd, "A", item_id, number, NULL);

	clif_displaymessage(fd, msg_txt(18)); // Item criado.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(item2)
{
	struct item item_tmp;
	struct item_data *item_data;
	char item_name[100];
	int item_id, number = 0;
	int identify = 0, refine = 0, attr = 0;
	int c1 = 0, c2 = 0, c3 = 0, c4 = 0;
	int flag;
	int loop, get_count, i;
	nullpo_retr(-1, sd);

	memset(item_name, '\0', sizeof(item_name));

	if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d %d %d %d %d %d %d %d", item_name, &number, &identify, &refine, &attr, &c1, &c2, &c3, &c4) < 9 &&
		sscanf(message, "%99s %d %d %d %d %d %d %d %d", item_name, &number, &identify, &refine, &attr, &c1, &c2, &c3, &c4) < 9
	)) {
		clif_displaymessage(fd, "Por favor, entre com todas as informações (uso: @item2 <nome/ID do item> <quantidade>");
		clif_displaymessage(fd, "  <flag_identificação> <refinamento> <atributo> <Carta1> <Carta2> <Carta3> <Carta4>).");
		return -1;
	}

	if (number <= 0)
		number = 1;

	item_id = 0;
	if ((item_data = itemdb_searchname(item_name)) != NULL ||
	    (item_data = itemdb_exists(atoi(item_name))) != NULL)
		item_id = item_data->nameid;

	if (item_id > 500) {
		loop = 1;
		get_count = number;
		if (item_data->type == IT_WEAPON || item_data->type == IT_ARMOR ||
			item_data->type == IT_PETEGG || item_data->type == IT_PETARMOR) {
			loop = number;
			get_count = 1;
			if (item_data->type == IT_PETEGG) {
				identify = 1;
				refine = 0;
			}
			if (item_data->type == IT_PETARMOR)
				refine = 0;
			if (refine > MAX_REFINE)
				refine = MAX_REFINE;
		} else {
			identify = 1;
			refine = attr = 0;
		}
		for (i = 0; i < loop; i++) {
			memset(&item_tmp, 0, sizeof(item_tmp));
			item_tmp.nameid = item_id;
			item_tmp.identify = identify;
			item_tmp.refine = refine;
			item_tmp.attribute = attr;
			item_tmp.card[0] = c1;
			item_tmp.card[1] = c2;
			item_tmp.card[2] = c3;
			item_tmp.card[3] = c4;
			if ((flag = pc_additem(sd, &item_tmp, get_count)))
				clif_additem(sd, 0, 0, flag);
		}

		//Logs (A)dmins items [Lupus]
		if(log_config.enable_logs&0x400)
			log_pick_pc(sd, "A", item_tmp.nameid, number, &item_tmp);

		clif_displaymessage(fd, msg_txt(18)); // Item criado.
	} else {
		clif_displaymessage(fd, msg_txt(19)); // Nome/ID inválido.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(costumeitem)
{
	struct item item_tmp;
	struct item_data *item_data;
	char item_name[100];
	int item_id, number = 0;
	int flag;
	int loop, get_count, i;
	nullpo_retr(-1, sd);

	memset(item_name, '\0', sizeof(item_name));

	if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\"%s %d", item_name, &number) < 1 &&
		sscanf(message, "%99s %d", item_name, &number) < 1
	)) {
		clif_displaymessage(fd, "Por favor, entre com todas as informações (uso: @costumeitem <nome/ID do item> <quantidade>).");
		return -1;
	}

	if (number <= 0)
		number = 1;

	item_id = 0;
	if ((item_data = itemdb_searchname(item_name)) != NULL ||
	    (item_data = itemdb_exists(atoi(item_name))) != NULL)
		item_id = item_data->nameid;

	if (item_id > 500) {
		loop = 1;
		get_count = number;
		if (!(item_data->equip&EQP_HEAD_LOW) &&
			!(item_data->equip&EQP_HEAD_LOW_C) &&
			!(item_data->equip&EQP_HEAD_MID) &&
			!(item_data->equip&EQP_HEAD_MID_C) &&
			!(item_data->equip&EQP_HEAD_TOP) &&
			!(item_data->equip&EQP_HEAD_TOP_C)
			)
		{
			clif_displaymessage(fd, "Este item não pode virar um costume.");
			return -1;
		}

		for (i = 0; i < loop; i++) {
			memset(&item_tmp, 0, sizeof(item_tmp));
			item_tmp.nameid = item_id;
			item_tmp.identify = 1;
			item_tmp.refine = 0;
			item_tmp.attribute = 5;
			item_tmp.card[0] = 0;
			item_tmp.card[1] = 0;
			item_tmp.card[2] = 0;
			item_tmp.card[3] = 0;
			if ((flag = pc_additem(sd, &item_tmp, get_count)))
				clif_additem(sd, 0, 0, flag);
		}

		//Logs (A)dmins items [Lupus]
		if(log_config.enable_logs&0x400)
			log_pick_pc(sd, "A", item_tmp.nameid, number, &item_tmp);

		clif_displaymessage(fd, msg_txt(18)); // Item criado.
	} else {
		clif_displaymessage(fd, msg_txt(19)); // Nome/ID inválido.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(itemreset)
{
	int i;
	nullpo_retr(-1, sd);

	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->status.inventory[i].amount && sd->status.inventory[i].equip == 0) {

			//Logs (A)dmins items [Lupus]
			if(log_config.enable_logs&0x400)
				log_pick_pc(sd, "A", sd->status.inventory[i].nameid, -sd->status.inventory[i].amount, &sd->status.inventory[i]);

			pc_delitem(sd, i, sd->status.inventory[i].amount, 0, 0);
		}
	}
	clif_displaymessage(fd, msg_txt(20)); //Todos os seus itens foram removidos

	return 0;
}

/*==========================================
 * Atcommand @lvlup
 *------------------------------------------*/
ACMD_FUNC(baselevelup)
{
	int level=0, i=0, status_point=0;
	nullpo_retr(-1, sd);
	level = atoi(message);

	if (!message || !*message || !level) {
		clif_displaymessage(fd, "Por favor, entre com o nível para ajustar (uso: @lvup/@blevel/@baselvlup <número de níveis>).");
		return -1;
	}

	if (level > 0) {
		if (sd->status.base_level == pc_maxbaselv(sd)) { //checa o nível máximo por Valaris
			clif_displaymessage(fd, msg_txt(47)); //Nível de Base não pode aumentar.
			return -1;
		} // Fim de adição
		if ((unsigned int)level > pc_maxbaselv(sd) || (unsigned int)level > pc_maxbaselv(sd) - sd->status.base_level) // fix positiv overflow
			level = pc_maxbaselv(sd) - sd->status.base_level;
		if (battle_config.use_statpoint_table)
			status_point += statp[sd->status.base_level+level] - statp[sd->status.base_level];
		else
		{
			for (i = 1; i <= level; i++)
				status_point += (sd->status.base_level + i + 14) / 5;
		}

		sd->status.status_point += status_point;
		sd->status.base_level += (unsigned int)level;
		status_percent_heal(&sd->bl, 100, 100);
		clif_misceffect(&sd->bl, 0);
		clif_displaymessage(fd, msg_txt(21)); // Nível de Base aumentado.
	} else {
		if (sd->status.base_level == 1) {
			clif_displaymessage(fd, msg_txt(158)); // Nível de base não pode diminuir
			return -1;
		}
		level*=-1;
		if ((unsigned int)level >= sd->status.base_level)
			level = sd->status.base_level-1;
		if (battle_config.use_statpoint_table)
			status_point += statp[sd->status.base_level] - statp[sd->status.base_level-level];
		else
		{
			for (i = 0; i > -level; i--)
				status_point += (sd->status.base_level + i + 14) / 5;
		}
		if (sd->status.status_point < status_point)
			pc_resetstate(sd);
		if (sd->status.status_point < status_point)
			sd->status.status_point = 0;
		else
			sd->status.status_point -= status_point;
		sd->status.base_level -= (unsigned int)level;
		clif_displaymessage(fd, msg_txt(22)); // Nível de base diminuído
	}
	sd->status.base_exp = 0;
	clif_updatestatus(sd, SP_STATUSPOINT);
	clif_updatestatus(sd, SP_BASELEVEL);
	clif_updatestatus(sd, SP_BASEEXP);
	clif_updatestatus(sd, SP_NEXTBASEEXP);
	status_calc_pc(sd, 0);
	if(sd->status.party_id)
		party_send_levelup(sd);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(joblevelup)
{
	int level=0;
	nullpo_retr(-1, sd);
	
	level = atoi(message);

	if (!message || !*message || !level) {
		clif_displaymessage(fd, "Por favor, entre com o nível para ajustar  (uso: @joblvup/@jlevel/@joblvlup <número de níveis>).");
		return -1;
	}
	if (level > 0) {
		if (sd->status.job_level == pc_maxjoblv(sd)) {
			clif_displaymessage(fd, msg_txt(23)); //Nível de Classe não pode ser aumentado.
			return -1;
		}
		if ((unsigned int)level > pc_maxjoblv(sd) || (unsigned int)level > pc_maxjoblv(sd) - sd->status.job_level) // fix positiv overflow
			level = pc_maxjoblv(sd) - sd->status.job_level;
		sd->status.job_level += (unsigned int)level;
		sd->status.skill_point += level;
		clif_misceffect(&sd->bl, 1);
		clif_displaymessage(fd, msg_txt(24)); // Nível de classe aumentado.
	} else {
		if (sd->status.job_level == 1) {
			clif_displaymessage(fd, msg_txt(159)); //Nível de classe não pode ser diminuído.
			return -1;
		}
		level *=-1;
		if ((unsigned int)level >= sd->status.job_level) // fix negativ overflow
			level = sd->status.job_level-1;
		sd->status.job_level -= (unsigned int)level;
		if (sd->status.skill_point < level)
			pc_resetskill(sd,0);	//Reseta skills desde que precisemos subtrair mais pontos.
		if (sd->status.skill_point < level)
			sd->status.skill_point = 0;
		else
			sd->status.skill_point -= level;
		clif_displaymessage(fd, msg_txt(25)); //Nível de classe diminuído.
	}
	sd->status.job_exp = 0;
	clif_updatestatus(sd, SP_JOBLEVEL);
	clif_updatestatus(sd, SP_JOBEXP);
	clif_updatestatus(sd, SP_NEXTJOBEXP);
	clif_updatestatus(sd, SP_SKILLPOINT);
	status_calc_pc(sd, 0);

	return 0;
}

/*==========================================
 * @help
 *------------------------------------------*/
ACMD_FUNC(help)
{
	char buf[2048], w1[2048], w2[2048];
	int i, gm_level;
	FILE* fp;
	nullpo_retr(-1, sd);

	memset(buf, '\0', sizeof(buf));

	if ((fp = fopen(help_txt, "r")) != NULL) {
		clif_displaymessage(fd, msg_txt(26)); // Comandos de @help:
		gm_level = pc_isGM(sd);
		while(fgets(buf, sizeof(buf), fp) != NULL) {
			if (buf[0] == '/' && buf[1] == '/')
				continue;
			for (i = 0; buf[i] != '\0'; i++) {
				if (buf[i] == '\r' || buf[i] == '\n') {
					buf[i] = '\0';
					break;
				}
			}
			if (sscanf(buf, "%2047[^:]:%2047[^\n]", w1, w2) < 2)
				clif_displaymessage(fd, buf);
			else if (gm_level >= atoi(w1))
				clif_displaymessage(fd, w2);
		}
		fclose(fp);
	} else {
		clif_displaymessage(fd, msg_txt(27)); // Arquivo help.txt não encontrado.
		return -1;
	}

	return 0;
}

/*==========================================
 * @help2 - comandos de char [Kayla]
 *------------------------------------------*/
ACMD_FUNC(help2)
{
	char buf[2048], w1[2048], w2[2048];
	int i, gm_level;
	FILE* fp;
	nullpo_retr(-1, sd);

	memset(buf, '\0', sizeof(buf));

	if ((fp = fopen(help2_txt, "r")) != NULL) {
		clif_displaymessage(fd, msg_txt(26)); //Comandos de ajuda:
		gm_level = pc_isGM(sd);
		while(fgets(buf, sizeof(buf), fp) != NULL) {
			if (buf[0] == '/' && buf[1] == '/')
				continue;
			for (i = 0; buf[i] != '\0'; i++) {
				if (buf[i] == '\r' || buf[i] == '\n') {
					buf[i] = '\0';
					break;
				}
			}
			if (sscanf(buf, "%2047[^:]:%2047[^\n]", w1, w2) < 2)
				clif_displaymessage(fd, buf);
			else if (gm_level >= atoi(w1))
				clif_displaymessage(fd, w2);
		}
		fclose(fp);
	} else {
		clif_displaymessage(fd, msg_txt(27)); //Arquivo help.txt não encontrado.
		return -1;
	}

	return 0;
}


//função auxiliar, usada para cada chamada parar timers de auto-attack
// parametro: '0' - todos, 'id' - apenas quem ataca alguém com esse id
static int atcommand_stopattack(struct block_list *bl,va_list ap)
{
	struct unit_data *ud = unit_bl2ud(bl);
	int id = va_arg(ap, int);
	if (ud && ud->attacktimer != INVALID_TIMER && (!id || id == ud->target))
	{
		unit_stop_attack(bl);
		return 1;
	}
	return 0;
}
/*==========================================
 *
 *------------------------------------------*/
static int atcommand_pvpoff_sub(struct block_list *bl,va_list ap)
{
	TBL_PC* sd = (TBL_PC*)bl;
	clif_pvpset(sd, 0, 0, 2);
	if (sd->pvp_timer != INVALID_TIMER) {
		delete_timer(sd->pvp_timer, pc_calc_pvprank_timer);
		sd->pvp_timer = INVALID_TIMER;
	}
	return 0;
}

ACMD_FUNC(pvpoff)
{
	nullpo_retr(-1, sd);

	if (!map[sd->bl.m].flag.pvp) {
		clif_displaymessage(fd, msg_txt(160)); //PvP já está desligado.
		return -1;
	}

	map[sd->bl.m].flag.pvp = 0;

	if (!battle_config.pk_mode)
		clif_map_property_mapall(sd->bl.m, MAPPROPERTY_NOTHING);
	map_foreachinmap(atcommand_pvpoff_sub,sd->bl.m, BL_PC);
	map_foreachinmap(atcommand_stopattack,sd->bl.m, BL_CHAR, 0);
	clif_displaymessage(fd, msg_txt(31)); // PvP: Off.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static int atcommand_pvpon_sub(struct block_list *bl,va_list ap)
{
	TBL_PC* sd = (TBL_PC*)bl;
	if (sd->pvp_timer == INVALID_TIMER) {
		sd->pvp_timer = add_timer(gettick() + 200, pc_calc_pvprank_timer, sd->bl.id, 0);
		sd->pvp_rank = 0;
		sd->pvp_lastusers = 0;
		sd->pvp_point = 5;
		sd->pvp_won = 0;
		sd->pvp_lost = 0;
	}
	return 0;
}

ACMD_FUNC(pvpon)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.pvp) {
		clif_displaymessage(fd, msg_txt(161)); //PvP já está ligado.
		return -1;
	}

	map[sd->bl.m].flag.pvp = 1;

	if (!battle_config.pk_mode)
	{
		clif_map_property_mapall(sd->bl.m, MAPPROPERTY_FREEPVPZONE);
		map_foreachinmap(atcommand_pvpon_sub,sd->bl.m, BL_PC);
	}

	clif_displaymessage(fd, msg_txt(32)); // PvP: On.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(gvgoff)
{
	nullpo_retr(-1, sd);

	if (!map[sd->bl.m].flag.gvg) {
		clif_displaymessage(fd, msg_txt(162)); // GvG já está desligada.
		return -1;
	}
		
	map[sd->bl.m].flag.gvg = 0;
	clif_map_property_mapall(sd->bl.m, MAPPROPERTY_NOTHING);
	map_foreachinmap(atcommand_stopattack,sd->bl.m, BL_CHAR, 0);
	clif_displaymessage(fd, msg_txt(33)); // GvG: Off.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(gvgon)
{
	nullpo_retr(-1, sd);

	if (map[sd->bl.m].flag.gvg) {
		clif_displaymessage(fd, msg_txt(163)); // GvG já está ligada
		return -1;
	}
	
	map[sd->bl.m].flag.gvg = 1;
	clif_map_property_mapall(sd->bl.m, MAPPROPERTY_AGITZONE);
	clif_displaymessage(fd, msg_txt(34)); // GvG: On.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(model)
{
	int hair_style = 0, hair_color = 0, cloth_color = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d %d %d", &hair_style, &hair_color, &cloth_color) < 1) {
		sprintf(atcmd_output, "Por favor, entre com pelo menos um valor (uso: @model <IDdoCabelo: %d-%d> <CorDoCabelo: %d-%d> <CorDasRoupas: %d-%d>).",
		        MIN_HAIR_STYLE, MAX_HAIR_STYLE, MIN_HAIR_COLOR, MAX_HAIR_COLOR, MIN_CLOTH_COLOR, MAX_CLOTH_COLOR);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (hair_style >= MIN_HAIR_STYLE && hair_style <= MAX_HAIR_STYLE &&
		hair_color >= MIN_HAIR_COLOR && hair_color <= MAX_HAIR_COLOR &&
		cloth_color >= MIN_CLOTH_COLOR && cloth_color <= MAX_CLOTH_COLOR) {
			pc_changelook(sd, LOOK_HAIR, hair_style);
			pc_changelook(sd, LOOK_HAIR_COLOR, hair_color);
			pc_changelook(sd, LOOK_CLOTHES_COLOR, cloth_color);
			clif_displaymessage(fd, msg_txt(36)); //Aparência alterada.
	} else {
		clif_displaymessage(fd, msg_txt(37)); // Número especificado inválido.
		return -1;
	}

	return 0;
}

/*==========================================
 * @dye && @ccolor
 *------------------------------------------*/
ACMD_FUNC(dye)
{
	int cloth_color = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &cloth_color) < 1) {
		sprintf(atcmd_output, "Por favor, entre com a cor da roupa (uso: @dye/@ccolor <CorDasRoupas: %d-%d>).", MIN_CLOTH_COLOR, MAX_CLOTH_COLOR);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (cloth_color >= MIN_CLOTH_COLOR && cloth_color <= MAX_CLOTH_COLOR) {
		pc_changelook(sd, LOOK_CLOTHES_COLOR, cloth_color);
		clif_displaymessage(fd, msg_txt(36)); // Aparência alterada.
	} else {
		clif_displaymessage(fd, msg_txt(37)); //Número especificado inválido.
		return -1;
	}

	return 0;
}

/*==========================================
 * @hairstyle && @hstyle
 *------------------------------------------*/
ACMD_FUNC(hair_style)
{
	int hair_style = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &hair_style) < 1) {
		sprintf(atcmd_output, "Por favor, entre com um estilo de cabelo (uso: @hairstyle/@hstyle <IDdoCabelo: %d-%d>).", MIN_HAIR_STYLE, MAX_HAIR_STYLE);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (hair_style >= MIN_HAIR_STYLE && hair_style <= MAX_HAIR_STYLE) {
			pc_changelook(sd, LOOK_HAIR, hair_style);
			clif_displaymessage(fd, msg_txt(36)); // Aparência alterada
	} else {
		clif_displaymessage(fd, msg_txt(37)); //Número especificado inválido.
		return -1;
	}

	return 0;
}

/*==========================================
 * @haircolor && @hcolor
 *------------------------------------------*/
ACMD_FUNC(hair_color)
{
	int hair_color = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &hair_color) < 1) {
		sprintf(atcmd_output, "Por favor, entre com uma cor de cabelo (uso: @haircolor/@hcolor <hair color: %d-%d>).", MIN_HAIR_COLOR, MAX_HAIR_COLOR);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	if (hair_color >= MIN_HAIR_COLOR && hair_color <= MAX_HAIR_COLOR) {
			pc_changelook(sd, LOOK_HAIR_COLOR, hair_color);
			clif_displaymessage(fd, msg_txt(36)); // Aparência alterada.
	} else {
		clif_displaymessage(fd, msg_txt(37)); //Número especificado inválido.
		return -1;
	}

	return 0;
}

/*==========================================
 * @go [número_da_cidade ou nome_da_cidade] - Atualizado por Harbin
 *------------------------------------------*/
ACMD_FUNC(go)
{
	int i;
	int town;
	char map_name[MAP_NAME_LENGTH];
	int m;
 
	const struct {
		char map[MAP_NAME_LENGTH];
		int x, y;
	} data[] = {
		{ MAP_PRONTERA,    156, 191 }, //  0=Prontera
		{ MAP_MORROC,      156,  93 }, //  1=Morroc
		{ MAP_GEFFEN,      119,  59 }, //  2=Geffen
		{ MAP_PAYON,       162, 233 }, //  3=Payon
		{ MAP_ALBERTA,     192, 147 }, //  4=Alberta
		{ MAP_IZLUDE,      128, 114 }, //  5=Izlude
		{ MAP_ALDEBARAN,   140, 131 }, //  6=Al de Baran
		{ MAP_LUTIE,       147, 134 }, //  7=Lutie
		{ MAP_COMODO,      209, 143 }, //  8=Comodo
		{ MAP_YUNO,        157,  51 }, //  9=Yuno
		{ MAP_AMATSU,      198,  84 }, // 10=Amatsu
		{ MAP_GONRYUN,     160, 120 }, // 11=Gonryun
		{ MAP_UMBALA,       89, 157 }, // 12=Umbala
		{ MAP_NIFLHEIM,     21, 153 }, // 13=Niflheim
		{ MAP_LOUYANG,     217,  40 }, // 14=Louyang
		{ MAP_NOVICE,       53, 111 }, // 15=Training Grounds
		{ MAP_JAIL,         23,  61 }, // 16=Prison
		{ MAP_JAWAII,      249, 127 }, // 17=Jawaii
		{ MAP_AYOTHAYA,    151, 117 }, // 18=Ayothaya
		{ MAP_EINBROCH,     64, 200 }, // 19=Einbroch
		{ MAP_LIGHTHALZEN, 158,  92 }, // 20=Lighthalzen
		{ MAP_EINBECH,      70,  95 }, // 21=Einbech
		{ MAP_HUGEL,        96, 145 }, // 22=Hugel
		{ MAP_RACHEL,      130, 110 }, // 23=Rachel
		{ MAP_VEINS,       216, 123 }, // 24=Veins
		{ MAP_MOSCOVIA,    223, 184 }, // 25=Moscovia
		{ MAP_BRASILIS,	   195,	218	}, // 26=Brasilis
		{ MAP_MANUK,	   295, 190	}, // 27=Manuk
		{ MAP_SPLENDIDE,   202,	150	}, // 28=Splendide
		{ MAP_DEWATA,      200, 180 }, // 29=Dewata
	};
 
	nullpo_retr(-1, sd);
 
	if( map[sd->bl.m].flag.nogo && battle_config.any_warp_GM_min_level > pc_isGM(sd) ) {
		clif_displaymessage(sd->fd,"Você não pode usar @go neste mapa.");
		return 0;
	}
 
	memset(map_name, '\0', sizeof(map_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));
 
	//pega o número
	town = atoi(message);
 
	// se não houver valor, mostra todos os valores
	if (!message || !*message || sscanf(message, "%11s", map_name) < 1 || town < 0 || town >= ARRAYLENGTH(data)) {
		clif_displaymessage(fd, msg_txt(38)); //Número de localização ou nome inválidos.
		clif_displaymessage(fd, msg_txt(82)); //Por Favor, use um desses nomes ou números:
		clif_displaymessage(fd, msg_txt(38)); //Número de localização ou nome inválidos.
		clif_displaymessage(fd, msg_txt(82)); //Por Favor, use um desses nomes ou números:
		clif_displaymessage(fd, "   0=Prontera                      1=Morroc       2=Geffen");
		clif_displaymessage(fd, "   3=Payon                          4=Alberta      5=Izlude");
		clif_displaymessage(fd, "   6=Al De Baran                  7=Lutie         8=Comodo");
		clif_displaymessage(fd, "   9=Juno                           10=Amatsu    11=Kunlun");
		clif_displaymessage(fd, " 12=Umbala                       13=Niflheim   14=Louyang");
		clif_displaymessage(fd, " 15=Campo de Aprendizes  16=Prisão      17=Jawaii");
		clif_displaymessage(fd, " 18=Ayothaya                    19=Einbroch  20=Lighthalzen");
		clif_displaymessage(fd, " 21=Einbech                      22=Hugel       23=Rachel");
		clif_displaymessage(fd, " 24=Veins                          25=Moscovia  26=Brasilis");
		clif_displaymessage(fd, " 27=Manuka                       28=Esplendor   29=Dewata");
		return -1;
	}

	// acha um possível nome para a cidade
	map_name[MAP_NAME_LENGTH-1] = '\0';
	for (i = 0; map_name[i]; i++)
		map_name[i] = TOLOWER(map_name[i]);
	// tenta identificar o nome do mapa
	if (strncmp(map_name, "prontera", 3) == 0) {
		town = 0;
	} else if (strncmp(map_name, "morocc", 3) == 0) {
		town = 1;
	} else if (strncmp(map_name, "geffen", 3) == 0) {
		town = 2;
	} else if (strncmp(map_name, "payon", 3) == 0 ||
	           strncmp(map_name, "paion", 3) == 0) {
		town = 3;
	} else if (strncmp(map_name, "alberta", 3) == 0) {
		town = 4;
	} else if (strncmp(map_name, "izlude", 3) == 0 ||
	           strncmp(map_name, "islude", 3) == 0) {
		town = 5;
	} else if (strncmp(map_name, "aldebaran", 3) == 0 ||
	           strcmp(map_name,  "al") == 0) {
		town = 6;
	} else if (strncmp(map_name, "lutie", 3) == 0 ||
	           strcmp(map_name,  "christmas") == 0 ||
	           strncmp(map_name, "xmas", 3) == 0 ||
	           strncmp(map_name, "x-mas", 3) == 0) {
		town = 7;
	} else if (strncmp(map_name, "comodo", 3) == 0) {
		town = 8;
	} else if (strncmp(map_name, "yuno", 3) == 0) {
		town = 9;
	} else if (strncmp(map_name, "amatsu", 3) == 0) {
		town = 10;
	} else if (strncmp(map_name, "gonryun", 3) == 0) {
		town = 11;
	} else if (strncmp(map_name, "umbala", 3) == 0) {
		town = 12;
	} else if (strncmp(map_name, "niflheim", 3) == 0) {
		town = 13;
	} else if (strncmp(map_name, "louyang", 3) == 0) {
		town = 14;
	} else if (strncmp(map_name, "new_1-1", 3) == 0 ||
	           strncmp(map_name, "startpoint", 3) == 0 ||
	           strncmp(map_name, "begining", 3) == 0 ||
			   strncmp(map_name, "campo de aprendiz", 3) == 0) {
		town = 15;
	} else if (strncmp(map_name, "sec_pri", 3) == 0 ||
	           strncmp(map_name, "prison", 3) == 0 ||
	           strncmp(map_name, "jails", 3) == 0) {
		town = 16;
	} else if (strncmp(map_name, "jawaii", 3) == 0 ||
	           strncmp(map_name, "jawai", 3) == 0) {
		town = 17;
	} else if (strncmp(map_name, "ayothaya", 3) == 0 ||
	           strncmp(map_name, "ayotaya", 3) == 0) {
		town = 18;
	} else if (strncmp(map_name, "einbroch", 5) == 0 ||
	           strncmp(map_name, "ainbroch", 3) == 0) {
		town = 19;
	} else if (strncmp(map_name, "lighthalzen", 3) == 0) {
		town = 20;
	} else if (strncmp(map_name, "einbech", 3) == 0) {
		town = 21;
	} else if (strncmp(map_name, "hugel", 3) == 0) {
		town = 22;
	} else if (strncmp(map_name, "rachel", 3) == 0) {
		town = 23;
	} else if (strncmp(map_name, "veins", 3) == 0) {
		town = 24;
	} else if (strncmp(map_name, "moscovia", 3) == 0) {
		town = 25;
	} else if (strncmp(map_name, "brasilis", 3) == 0) {
		town = 26;
	} else if (strncmp(map_name, "manuk", 3) == 0) {
		town = 27;
	} else if (strncmp(map_name, "esplendide", 3) == 0) {
		town = 28;
	} else if (strncmp(map_name, "dewata", 3) == 0) {
		town = 29;
	}

	if (town >= 0 && town < ARRAYLENGTH(data))
	{
		m = map_mapname2mapid(data[town].map);
		if (m >= 0 && map[m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
			clif_displaymessage(fd, msg_txt(247));
			return -1;
		}
		if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
			clif_displaymessage(fd, msg_txt(248));
			return -1;
		}
		if (pc_setpos(sd, mapindex_name2id(data[town].map), data[town].x, data[town].y, CLR_TELEPORT) == 0) {
			clif_displaymessage(fd, msg_txt(0)); // Teletransportado.
		} else {
			clif_displaymessage(fd, msg_txt(1)); // Mapa não encontrado.
			return -1;
		}
	} else { // Se você chegar aqui, você tem um erro na variável dos nomes quando se lê os nomes 
  		clif_displaymessage(fd, msg_txt(38)); // Invalid location number or name.
		return -1;
	}
 
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(monster)
{
	char name[NAME_LENGTH];
	char monster[NAME_LENGTH];
	int mob_id;
	int number = 0;
	int count;
	int i, k, range;
	short mx, my;
	nullpo_retr(-1, sd);

	memset(name, '\0', sizeof(name));
	memset(monster, '\0', sizeof(monster));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message) {
			clif_displaymessage(fd, msg_txt(80)); // Dê um nome de exibição e um nome/id de um monstro por favor.
			return -1;
	}
	if (sscanf(message, "\"%23[^\"]\" %23s %d", name, monster, &number) > 1 ||
		sscanf(message, "%23s \"%23[^\"]\" %d", monster, name, &number) > 1) {
		//Todos os datos podem ser daixados como estão.
	} else if ((count=sscanf(message, "%23s %d %23s", monster, &number, name)) > 1) {
		//Aqui, provavelmente o nome não foi dado e nós estamos usando um monstro para isso.
		if (count < 3) //Nome do monstro em branco
			name[0] = '\0';
	} else if (sscanf(message, "%23s %23s %d", name, monster, &number) > 1) {
		//Todos os datos podem ser daixados como estão.
	} else if (sscanf(message, "%23s", monster) > 0) {
		//Como antes, o nome pode já estar preenchido.
		name[0] = '\0';
	} else {
		clif_displaymessage(fd, msg_txt(80)); // Dê um nome de exibição e um nome/id de um monstro por favor.
		return -1;
	}

	if ((mob_id = mobdb_searchname(monster)) == 0) // checa o nome primeiro (para evitar um possível nome começado com um número)
		mob_id = mobdb_checkid(atoi(monster));

	if (mob_id == 0) {
		clif_displaymessage(fd, msg_txt(40)); // Nome ou ID de monstro inválido.
		return -1;
	}

	if (mob_id == MOBID_EMPERIUM) {
		clif_displaymessage(fd, msg_txt(83)); //O monstro 'Emperium' não pode ser criado.
		return -1;
	}

	if (number <= 0)
		number = 1;

	if( !name[0] )
		strcpy(name, "--ja--");

	if (battle_config.atc_spawn_quantity_limit && number > battle_config.atc_spawn_quantity_limit)
		number = battle_config.atc_spawn_quantity_limit;

	if (battle_config.etc_log)
		ShowInfo("%s monster='%s' name='%s' id=%d count=%d (%d,%d)\n", command, monster, name, mob_id, number, sd->bl.x, sd->bl.y);

	count = 0;
	range = (int)sqrt((float)number) +2; //cálculo de um número ímpar(+ 4 area ao redor)
	for (i = 0; i < number; i++) {
		map_search_freecell(&sd->bl, 0, &mx,  &my, range, range, 0);
		k = mob_once_spawn(sd, sd->bl.m, mx, my, name, mob_id, 1, "");
		count += (k != 0) ? 1 : 0;
	}

	if (count != 0)
		if (number == count)
			clif_displaymessage(fd, msg_txt(39)); // Todos os monstros foram criados!
		else {
			sprintf(atcmd_output, msg_txt(240), count); // %d monstros sumonados !
			clif_displaymessage(fd, atcmd_output);
		}
	else {
		clif_displaymessage(fd, msg_txt(40)); //Nome/ID de monstro inválido.
		return -1;
	}

	return 0;
}

//spawn de monstros pequenos [Valaris]
ACMD_FUNC(monstersmall)
{
	char name[NAME_LENGTH] = "";
	char monster[NAME_LENGTH] = "";
	int mob_id = 0;
	int number = 0;
	int x = 0;
	int y = 0;
	int count;
	int i;

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Dê o nome ou o ID de um monstro por favor.");
		return -1;
	}

	if (sscanf(message, "\"%23[^\"]\" %23s %d %d %d", name, monster, &number, &x, &y) < 2 &&
	    sscanf(message, "%23s \"%23[^\"]\" %d %d %d", monster, name, &number, &x, &y) < 2 &&
	    sscanf(message, "%23s %d %23s %d %d", monster, &number, name, &x, &y) < 1) {
		clif_displaymessage(fd, "Dê o nome ou o ID de um monstro por favor.");
		return -1;
	}

	//Se o argumento de identificação do monstro for um nome
    if ((mob_id = mobdb_searchname(monster)) == 0) // checa o nome primeiro(para evitar um possível nome começado com número)
		mob_id = atoi(monster);

	if (mob_id == 0) {
		clif_displaymessage(fd, msg_txt(40));
		return -1;
	}

	if (mob_id == MOBID_EMPERIUM) {
		clif_displaymessage(fd, msg_txt(83));	//O monstro 'Emperium' não pode ser criado.
		return -1;
	}

	if (mobdb_checkid(mob_id) == 0) {
		clif_displaymessage(fd, "ID de monstro inválido, tente novamente.");
		return -1;
	}

	if (number <= 0)
		number = 1;

	if( !name[0] )
		strcpy(name, "--ja--");

	if (battle_config.atc_spawn_quantity_limit >= 1 && number > battle_config.atc_spawn_quantity_limit)
		number = battle_config.atc_spawn_quantity_limit;

	count = 0;
	for (i = 0; i < number; i++) {
		int mx, my;
		if (x <= 0)
			mx = sd->bl.x + (rand() % 11 - 5);
		else
			mx = x;
		if (y <= 0)
			my = sd->bl.y + (rand() % 11 - 5);
		else
			my = y;
		count += (mob_once_spawn(sd, sd->bl.m, mx, my, name, mob_id, 1, "2") != 0) ? 1 : 0;
	}

	if (count != 0)
		clif_displaymessage(fd, msg_txt(39)); // Monstro criado !
	else
    		clif_displaymessage(fd, msg_txt(40)); // Nome ou ID de monstro inválido.

	return 0;
}
// spawn de monstros grandes [Valaris]
ACMD_FUNC(monsterbig)
{
	char name[NAME_LENGTH] = "";
	char monster[NAME_LENGTH] = "";
	int mob_id = 0;
	int number = 0;
	int x = 0;
	int y = 0;
	int count;
	int i;

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Dê o nome ou o ID de um monstro por favor.");
		return -1;
	}

	if (sscanf(message, "\"%23[^\"]\" %23s %d %d %d", name, monster, &number, &x, &y) < 2 &&
	    sscanf(message, "%23s \"%23[^\"]\" %d %d %d", monster, name, &number, &x, &y) < 2 &&
	    sscanf(message, "%23s %d %23s %d %d", monster, &number, name, &x, &y) < 1) {
		clif_displaymessage(fd, "Dê o nome ou o ID de um monstro por favor.");
		return -1;
	}

	//Se o argumento de identificação do monstro for um nome
	if ((mob_id = mobdb_searchname(monster)) == 0) //checa o nome primeiro(para evitar um possível nome começado com número)
		mob_id = atoi(monster);

	if (mob_id == 0) {
		clif_displaymessage(fd, msg_txt(40));
		return -1;
	}

	if (mob_id == MOBID_EMPERIUM) {
		clif_displaymessage(fd, msg_txt(83));	//Não se pode criar um emperium;
		return -1;
	}

	if (mobdb_checkid(mob_id) == 0) {
		clif_displaymessage(fd, "Invalid monster ID");
		return -1;
	}

	if (number <= 0)
		number = 1;

	if( !name[0] )
		strcpy(name, "--ja--");
		
	if (battle_config.atc_spawn_quantity_limit >= 1 && number > battle_config.atc_spawn_quantity_limit)
		number = battle_config.atc_spawn_quantity_limit;

	count = 0;
	for (i = 0; i < number; i++) {
		int mx, my;
		if (x <= 0)
			mx = sd->bl.x + (rand() % 11 - 5);
		else
			mx = x;
		if (y <= 0)
			my = sd->bl.y + (rand() % 11 - 5);
		else
			my = y;
		count += (mob_once_spawn(sd, sd->bl.m, mx, my, name, mob_id, 1, "4") != 0) ? 1 : 0;
	}

	if (count != 0)
		clif_displaymessage(fd, msg_txt(39)); // Monstro criado !
	else
		clif_displaymessage(fd, msg_txt(40)); // Nome ou ID de monstro inválido.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static int atkillmonster_sub(struct block_list *bl, va_list ap)
{
	struct mob_data *md;
	int flag;
	
	nullpo_ret(md=(struct mob_data *)bl);
	flag = va_arg(ap, int);

	if (md->guardian_data)
		return 0; //Não toque nos guardiões da GdE!
	
	if (flag)
		status_zap(bl,md->status.hp, 0);
	else
		status_kill(bl);
	return 1;
}

void atcommand_killmonster_sub(const int fd, struct map_session_data* sd, const char* message, const int drop)
{
	int map_id;
	char map_name[MAP_NAME_LENGTH_EXT];

	if (!sd) return;

	memset(map_name, '\0', sizeof(map_name));

	if (!message || !*message || sscanf(message, "%15s", map_name) < 1)
		map_id = sd->bl.m;
	else {
		if ((map_id = map_mapname2mapid(map_name)) < 0)
			map_id = sd->bl.m;
	}

	map_foreachinmap(atkillmonster_sub, map_id, BL_MOB, drop);

	clif_displaymessage(fd, msg_txt(165)); // Todos os monstros foram mortos!

	return;
}

ACMD_FUNC(killmonster)
{
	atcommand_killmonster_sub(fd, sd, message, 1);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(killmonster2)
{
	atcommand_killmonster_sub(fd, sd, message, 0);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(refine)
{
	int i,j, position = 0, refine = 0, current_position, final_refine;
	int count;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d %d", &position, &refine) < 2) {
		clif_displaymessage(fd, "Por favor, entre com uma posição e com uma quantidade (uso: @refine <posição do equip> <+/- quantidade>).");
		sprintf(atcmd_output, "%d: Cabeça-baixo", EQP_HEAD_LOW);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Mão direita", EQP_HAND_R);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Capa", EQP_GARMENT);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Acessório esquerdo", EQP_ACC_L);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Armadura", EQP_ARMOR);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Mão esquerda", EQP_HAND_L);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Sapatos", EQP_SHOES);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Acessório direito", EQP_ACC_R);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Cabeça-topo", EQP_HEAD_TOP);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, "%d: Cabeça-médio", EQP_HEAD_MID);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	refine = cap_value(refine, -MAX_REFINE, MAX_REFINE);

	count = 0;
	for (j = 0; j < EQI_MAX-1; j++) {
		if ((i = sd->equip_index[j]) < 0)
			continue;
		if(j == EQI_HAND_R && sd->equip_index[EQI_HAND_L] == i)
			continue;
		if(j == EQI_HEAD_MID && sd->equip_index[EQI_HEAD_LOW] == i)
			continue;
		if(j == EQI_HEAD_TOP && (sd->equip_index[EQI_HEAD_MID] == i || sd->equip_index[EQI_HEAD_LOW] == i))
			continue;

		if(position && !(sd->status.inventory[i].equip & position))
			continue;

		final_refine = cap_value(sd->status.inventory[i].refine + refine, 0, MAX_REFINE);
		if (sd->status.inventory[i].refine != final_refine) {
			sd->status.inventory[i].refine = final_refine;
			current_position = sd->status.inventory[i].equip;
			pc_unequipitem(sd, i, 3);
			clif_refine(fd, 0, i, sd->status.inventory[i].refine);
			clif_delitem(sd, i, 1, 3);
			clif_additem(sd, i, 1, 0);
			pc_equipitem(sd, i, current_position);
			clif_misceffect(&sd->bl, 3);
			count++;
		}
	}

	if (count == 0)
		clif_displaymessage(fd, msg_txt(166)); // Nenhum item foi refinado.
	else if (count == 1)
		clif_displaymessage(fd, msg_txt(167)); // 1 item foi refinado!
	else {
		sprintf(atcmd_output, msg_txt(168), count); // %d itens foram refinados!
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(produce)
{
	char item_name[100];
	int item_id, attribute = 0, star = 0;
	int flag = 0;
	struct item_data *item_data;
	struct item tmp_item;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(item_name, '\0', sizeof(item_name));

	if (!message || !*message || (
		sscanf(message, "\"%99[^\"]\" %d %d", item_name, &attribute, &star) < 1 &&
		sscanf(message, "%99s %d %d", item_name, &attribute, &star) < 1
	)) {
		clif_displaymessage(fd, "Por favor, entre com pelo menos um id/nome de um item (uso: @produce <nome/id> <elemento> <quantidade>).");
		return -1;
	}

	item_id = 0;
	if ((item_data = itemdb_searchname(item_name)) == NULL &&
	    (item_data = itemdb_exists(atoi(item_name))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(170)); //Este item não é um equipamento.
		return -1;
	}
	item_id = item_data->nameid;
	if (itemdb_isequip2(item_data)) {
		if (attribute < MIN_ATTRIBUTE || attribute > MAX_ATTRIBUTE)
			attribute = ATTRIBUTE_NORMAL;
		if (star < MIN_STAR || star > MAX_STAR)
			star = 0;
		memset(&tmp_item, 0, sizeof tmp_item);
		tmp_item.nameid = item_id;
		tmp_item.amount = 1;
		tmp_item.identify = 1;
		tmp_item.card[0] = CARD0_FORGE;
		tmp_item.card[1] = item_data->type==IT_WEAPON?
			((star*5) << 8) + attribute:0;
		tmp_item.card[2] = GetWord(sd->status.char_id, 0);
		tmp_item.card[3] = GetWord(sd->status.char_id, 1);
		clif_produceeffect(sd, 0, item_id);
		clif_misceffect(&sd->bl, 3);

		//Logs (A)dmins items [Lupus]
		if(log_config.enable_logs&0x400)
			log_pick_pc(sd, "A", tmp_item.nameid, 1, &tmp_item);

		if ((flag = pc_additem(sd, &tmp_item, 1)))
			clif_additem(sd, 0, 0, flag);
	} else {
		sprintf(atcmd_output, msg_txt(169), item_id, item_data->name); // O item (%d: '%s') não é equipável
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(memo)
{
	int position = 0;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if( !message || !*message || sscanf(message, "%d", &position) < 1 )
	{
		int i;
		clif_displaymessage(sd->fd,  "Sua posição memo atual é:");
		for( i = 0; i < MAX_MEMOPOINTS; i++ )
		{
			if( sd->status.memo_point[i].map )
				sprintf(atcmd_output, "%d - %s (%d,%d)", i, mapindex_id2name(sd->status.memo_point[i].map), sd->status.memo_point[i].x, sd->status.memo_point[i].y);
			else
				sprintf(atcmd_output, msg_txt(171), i); // %d - void
			clif_displaymessage(sd->fd, atcmd_output);
 		}
		return 0;
 	}
 
	if( position < 0 || position >= MAX_MEMOPOINTS )
	{
		sprintf(atcmd_output, "Por favor, entre com uma posição válida (uso: @memo <memo_posição:%d-%d>).", 0, MAX_MEMOPOINTS-1);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	pc_memo(sd, position);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(gat)
{
	int y;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	for (y = 2; y >= -2; y--) {
		sprintf(atcmd_output, "%s (x= %d, y= %d) %02X %02X %02X %02X %02X",
			map[sd->bl.m].name,   sd->bl.x - 2, sd->bl.y + y,
 			map_getcell(sd->bl.m, sd->bl.x - 2, sd->bl.y + y, CELL_GETTYPE),
 			map_getcell(sd->bl.m, sd->bl.x - 1, sd->bl.y + y, CELL_GETTYPE),
 			map_getcell(sd->bl.m, sd->bl.x,     sd->bl.y + y, CELL_GETTYPE),
 			map_getcell(sd->bl.m, sd->bl.x + 1, sd->bl.y + y, CELL_GETTYPE),
 			map_getcell(sd->bl.m, sd->bl.x + 2, sd->bl.y + y, CELL_GETTYPE));

		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(displaystatus)
{
	int i, type, flag, tick, val1, val2, val3;
	nullpo_retr(-1, sd);
	
	if (!message || !*message || (i = sscanf(message, "%d %d %d %d %d %d", &type, &flag, &tick, &val1, &val2, &val3)) < 1) {
		clif_displaymessage(fd, "Por favor, entre com um tipo/flag do status (uso: @displaystatus <tipo status> <flag> <tick> <val1> <val2> <val3>).");
		return -1;
	}
	if (i < 2) flag = 1;
	if (i < 3) tick = 0;

	clif_status_change(&sd->bl, type, flag, tick, val1, val2, val3);

	return 0;
}

/*==========================================
 * @stpoint (Reescrito por [Yor])
 *------------------------------------------*/
ACMD_FUNC(statuspoint)
{
	int point;
	unsigned int new_status_point;

	if (!message || !*message || (point = atoi(message)) == 0) {
		clif_displaymessage(fd, "Por favor, entre com um número(uso: @stpoint <número de pontos>).");
		return -1;
	}

	if(point < 0)
	{
		if(sd->status.status_point < (unsigned int)(-point))
		{
			new_status_point = 0;
		}
		else
		{
			new_status_point = sd->status.status_point + point;
		}
	}
	else if(UINT_MAX - sd->status.status_point < (unsigned int)point)
	{
		new_status_point = UINT_MAX;
	}
	else
	{
		new_status_point = sd->status.status_point + point;
	}

	if (new_status_point != sd->status.status_point) {
		sd->status.status_point = new_status_point;
		clif_updatestatus(sd, SP_STATUSPOINT);
		clif_displaymessage(fd, msg_txt(174)); // Números de status alterados.
	} else {
		if (point < 0)
			clif_displaymessage(fd, msg_txt(41)); // Impossível diminuir o valor.
		else
			clif_displaymessage(fd, msg_txt(149)); // Impossível aumentar o valor.
		return -1;
	}

	return 0;
}

/*==========================================
 * @skpoint (Reescrito por [Yor])
 *------------------------------------------*/
ACMD_FUNC(skillpoint)
{
	int point;
	unsigned int new_skill_point;
	nullpo_retr(-1, sd);

	if (!message || !*message || (point = atoi(message)) == 0) {
		clif_displaymessage(fd, "Por favor, entre com um número(uso: @skpoint <número de pontos>).");
		return -1;
	}

	if(point < 0)
	{
		if(sd->status.skill_point < (unsigned int)(-point))
		{
			new_skill_point = 0;
		}
		else
		{
			new_skill_point = sd->status.skill_point + point;
		}
	}
	else if(UINT_MAX - sd->status.skill_point < (unsigned int)point)
	{
		new_skill_point = UINT_MAX;
	}
	else
	{
		new_skill_point = sd->status.skill_point + point;
	}

	if (new_skill_point != sd->status.skill_point) {
		sd->status.skill_point = new_skill_point;
		clif_updatestatus(sd, SP_SKILLPOINT);
		clif_displaymessage(fd, msg_txt(175)); // Número de pontos de skill alterados..
	} else {
		if (point < 0)
			clif_displaymessage(fd, msg_txt(41)); // Impossível diminuir o valor.
		else
			clif_displaymessage(fd, msg_txt(149)); // Impossível aumentar o valor.
		return -1;
	}

	return 0;
}

/*==========================================
 * @zeny (Reescrito por [Yor])
 *------------------------------------------*/
ACMD_FUNC(zeny)
{
	int zeny, new_zeny;
	nullpo_retr(-1, sd);

	if (!message || !*message || (zeny = atoi(message)) == 0) {
		clif_displaymessage(fd, "Por favor, entre com uma quantidade (uso: @zeny <quantidade>).");
		return -1;
	}

	new_zeny = sd->status.zeny + zeny;
	if (zeny > 0 && (zeny > MAX_ZENY || new_zeny > MAX_ZENY)) // fix positiv overflow
		new_zeny = MAX_ZENY;
	else if (zeny < 0 && (zeny < -MAX_ZENY || new_zeny < 0)) // fix negativ overflow
		new_zeny = 0;

	if (new_zeny != sd->status.zeny) {
		sd->status.zeny = new_zeny;
		clif_updatestatus(sd, SP_ZENY);
		clif_displaymessage(fd, msg_txt(176)); //Quantidade de zeny atual alterada.
	} else {
		if (zeny < 0)
			clif_displaymessage(fd, msg_txt(41)); //Impossível diminuir o valor.
		else
			clif_displaymessage(fd, msg_txt(149)); //Impossível aumentar o valor.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(param)
{
	int i, value = 0, new_value;
	const char* param[] = { "str", "agi", "vit", "int", "dex", "luk" };
	short* status[6];
 	//Nós não usamos inicialização direta pois não é parte do padrão de C
	nullpo_retr(-1, sd);
	
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%d", &value) < 1 || value == 0) {
		sprintf(atcmd_output, "Por favor, entre com um valor válido (uso: @str,@agi,@vit,@int,@dex,@luk <+/-ajustes>).");
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	ARR_FIND( 0, ARRAYLENGTH(param), i, strcmpi(command+1, param[i]) == 0 );

	if( i == ARRAYLENGTH(param) || i > MAX_STATUS_TYPE) { // normalmente impossível
		sprintf(atcmd_output, "Por favor, entre com um valor válido (uso: @str,@agi,@vit,@int,@dex,@luk <+/-adjustment>).");
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	status[0] = &sd->status.str;
	status[1] = &sd->status.agi;
	status[2] = &sd->status.vit;
	status[3] = &sd->status.int_;
	status[4] = &sd->status.dex;
	status[5] = &sd->status.luk;

	if(value < 0 && *status[i] <= -value)
	{
		new_value = 1;
	}
	else if(SHRT_MAX - *status[i] < value)
	{
		new_value = SHRT_MAX;
	}
	else
	{
		new_value = *status[i] + value;
	}

	if (new_value != *status[i]) {
		*status[i] = new_value;
		clif_updatestatus(sd, SP_STR + i);
		clif_updatestatus(sd, SP_USTR + i);
		status_calc_pc(sd, 0);
		clif_displaymessage(fd, msg_txt(42)); // Status alterado.
	} else {
		if (value < 0)
			clif_displaymessage(fd, msg_txt(41)); //Impossível diminuir o valor.
		else
			clif_displaymessage(fd, msg_txt(149)); //Impossível aumentar o valor.
		return -1;
	}

	return 0;
}

/*==========================================
 * Stat all by fritz (reescrito por [Yor])
 *------------------------------------------*/
ACMD_FUNC(stat_all)
{
	int index, count, value, max, new_value;
	short* status[6];
 	//Nós não usamos inicialização direta pois não é parte do padrão de C.
	nullpo_retr(-1, sd);
	
	status[0] = &sd->status.str;
	status[1] = &sd->status.agi;
	status[2] = &sd->status.vit;
	status[3] = &sd->status.int_;
	status[4] = &sd->status.dex;
	status[5] = &sd->status.luk;

	if( !message || !*message || sscanf(message, "%d", &value) < 1 || value == 0 )
	{
		value = pc_maxparameter(sd);
		max = pc_maxparameter(sd);
	}
	else
	{
		max = SHRT_MAX;
	}

	count = 0;
	for (index = 0; index < ARRAYLENGTH(status); index++) {

		if (value > 0 && *status[index] > max - value)
			new_value = max;
		else if (value < 0 && *status[index] <= -value)
			new_value = 1;
		else
			new_value = *status[index] +value;
		
		if (new_value != (int)*status[index]) {
			*status[index] = new_value;
			clif_updatestatus(sd, SP_STR + index);
			clif_updatestatus(sd, SP_USTR + index);
			count++;
		}
	}

	if (count > 0) { // se pelo menos um status foi modificado
		status_calc_pc(sd, 0);
		clif_displaymessage(fd, msg_txt(84)); // Todos os status alterados.
	} else {
		if (value < 0)
			clif_displaymessage(fd, msg_txt(177)); // Você não pode diminuir esse status mais.
		else
			clif_displaymessage(fd, msg_txt(178)); // Você não pode aumentar esse status mais.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(guildlevelup)
{
	int level = 0;
	short added_level;
	struct guild *guild_info;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d", &level) < 1 || level == 0) {
		clif_displaymessage(fd, "Por favor, entre com um nível válido (uso: @guildlvup/@guildlvlup <# de níveis>).");
		return -1;
	}

	if (sd->status.guild_id <= 0 || (guild_info = guild_search(sd->status.guild_id)) == NULL) {
		clif_displaymessage(fd, msg_txt(43)); // Você não está em um clã.
		return -1;
	}
	//if (strcmp(sd->status.name, guild_info->master) != 0) {
	//	clif_displaymessage(fd, msg_txt(44)); 
	//	return -1;
	//}

	added_level = (short)level;
	if (level > 0 && (level > MAX_GUILDLEVEL || added_level > ((short)MAX_GUILDLEVEL - guild_info->guild_lv))) // fix positiv overflow
		added_level = (short)MAX_GUILDLEVEL - guild_info->guild_lv;
	else if (level < 0 && (level < -MAX_GUILDLEVEL || added_level < (1 - guild_info->guild_lv))) // fix negativ overflow
		added_level = 1 - guild_info->guild_lv;

	if (added_level != 0) {
		intif_guild_change_basicinfo(guild_info->guild_id, GBI_GUILDLV, &added_level, sizeof(added_level));
		clif_displaymessage(fd, msg_txt(179)); // Nível de clã alterado.
	} else {
		clif_displaymessage(fd, msg_txt(45)); //Mudança de nível de clã falhou.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(makeegg)
{
	struct item_data *item_data;
	int id, pet_id;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com um monstro/nome de ovo/id (uso: @makeegg <pet>).");
		return -1;
	}

	if ((item_data = itemdb_searchname(message)) != NULL) //para nome de ovo
		id = item_data->nameid;
	else
	if ((id = mobdb_searchname(message)) != 0) // para nome de monstro
		;
	else
		id = atoi(message);

	pet_id = search_petDB_index(id, PET_CLASS);
	if (pet_id < 0)
		pet_id = search_petDB_index(id, PET_EGG);
	if (pet_id >= 0) {
		sd->catch_target_class = pet_db[pet_id].class_;
		intif_create_pet(
			sd->status.account_id, sd->status.char_id,
			(short)pet_db[pet_id].class_, (short)mob_db(pet_db[pet_id].class_)->lv,
			(short)pet_db[pet_id].EggID, 0, (short)pet_db[pet_id].intimate,
			100, 0, 1, pet_db[pet_id].jname);
	} else {
		clif_displaymessage(fd, msg_txt(180)); // O monstro/nome de ovo/id não existe.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(hatch)
{
	nullpo_retr(-1, sd);
	if (sd->status.pet_id <= 0)
		clif_sendegg(sd);
	else {
		clif_displaymessage(fd, msg_txt(181)); // Você já tem um pet.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(petfriendly)
{
	int friendly;
	struct pet_data *pd;
	nullpo_retr(-1, sd);

	if (!message || !*message || (friendly = atoi(message)) < 0) {
		clif_displaymessage(fd, "Por favor, entre um valor válido (uso: @petfriendly <0-1000>).");
		return -1;
	}

	pd = sd->pd;
	if (!pd) {
		clif_displaymessage(fd, msg_txt(184)); // Desculpe, mas você não tem pet
		return -1;
	}
	
	if (friendly < 0 || friendly > 1000)
	{
		clif_displaymessage(fd, msg_txt(37)); //O número especificado é inválido.
		return -1;
	}
	
	if (friendly == pd->pet.intimate) {
		clif_displaymessage(fd, msg_txt(183)); // A intimidade do pet já está no máximo.
		return -1;
	}

	pet_set_intimate(pd, friendly);
	clif_send_petstatus(sd);
	clif_displaymessage(fd, msg_txt(182)); // Intimidade do pet alterada.
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(pethungry)
{
	int hungry;
	struct pet_data *pd;
	nullpo_retr(-1, sd);

	if (!message || !*message || (hungry = atoi(message)) < 0) {
		clif_displaymessage(fd, "Por favor, entre com um número válido(uso: @pethungry <0-100>).");
		return -1;
	}

	pd = sd->pd;
	if (!sd->status.pet_id || !pd) {
		clif_displaymessage(fd, msg_txt(184)); // Desculpe, mas você não tem pet.
		return -1;
	}
	if (hungry < 0 || hungry > 100) {
		clif_displaymessage(fd, msg_txt(37)); //O número especificado é inválido.
		return -1;
	}
	if (hungry == pd->pet.hungry) {
		clif_displaymessage(fd, msg_txt(186)); //A fome do pet já está ao máximo.
		return -1;
	}

	pd->pet.hungry = hungry;
	clif_send_petstatus(sd);
	clif_displaymessage(fd, msg_txt(185)); // Fome do pet alterada.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(petrename)
{
	struct pet_data *pd;
	nullpo_retr(-1, sd);
	if (!sd->status.pet_id || !sd->pd) {
		clif_displaymessage(fd, msg_txt(184)); //Desculpe, mas você não tem pet.
		return -1;
	}
	pd = sd->pd;
	if (!pd->pet.rename_flag) {
		clif_displaymessage(fd, msg_txt(188)); // Você já pode renomear seu pet.
		return -1;
	}

	pd->pet.rename_flag = 0;
	intif_save_petdata(sd->status.account_id, &pd->pet);
	clif_send_petstatus(sd);
	clif_displaymessage(fd, msg_txt(187)); // Você pode renomear seu pet agora.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(recall)
{
	struct map_session_data *pl_sd = NULL;

	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um player (uso: @recall <nome/id>).");
		return -1;
	}

	if((pl_sd=map_nick2sd((char *)message)) == NULL && (pl_sd=map_charid2sd(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}
		
	if (pl_sd == sd)
	{
		clif_displaymessage(fd, "Você já está onde você está !");
		return -1;
	}

	if ( pc_isGM(sd) < pc_isGM(pl_sd) )
	{
		clif_displaymessage(fd, msg_txt(81)); // Your GM level doesn't authorize you to preform this action on the specified player.
		return -1;
	}
	
	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, "Você não está autorizado a teletransportar alguém ao seu mapa atual.");
		return -1;
	}
	if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, "Você não está autorizado a teletransportar este player de seu mapa atual.");
		return -1;
	}
	pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_RESPAWN);
	sprintf(atcmd_output, msg_txt(46), pl_sd->status.name); // %s chamado!
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * comando charblock(uso: charblock <player_name>)
 * Este comando bane definitivamente um jogador.
 *------------------------------------------*/
ACMD_FUNC(char_block)
{
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com um nome de um jogador (uso: @charblock/@block <nome>).");
		return -1;
	}

	chrif_char_ask_name(sd->status.account_id, atcmd_player_name, 1, 0, 0, 0, 0, 0, 0); // type: 1 - block
	clif_displaymessage(fd, msg_txt(88)); //Nome do personagem enviado ao servidor de personagens (char-server)

	return 0;
}

/*==========================================
 * comando charban (uso: charban <tempo> <nome>)
 * Este comando bane um jogador por tempo determinado
 * O tempo é feito assim:
 *   Valor de ajuste (-1, 1, +1, etc...)
 *   Elemento modificável:
 *     a ou y: anos
 *     m:  meses
 *     j or d: dias
 *     h:  horas
 *     mn: minutos
 *     s:  segundos
 * <exemplo> @ban +1m-2mn1s-6y teste_jogador
 *           este exemplo adiciona 1 mês e 1 segundo, e subtrai 2 minutos e 6 anos ao mesmo tempo.
 *------------------------------------------*/
ACMD_FUNC(char_ban)
{
	char * modif_p;
	int year, month, day, hour, minute, second, value;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%s %23[^\n]", atcmd_output, atcmd_player_name) < 2) {
		clif_displaymessage(fd, "Por favor, entre com o tempo de ban e o nome do jogador (uso: @charban/@ban/@banish/@charbanish <tempo> <nome>).");
		return -1;
	}

	atcmd_output[sizeof(atcmd_output)-1] = '\0';

	modif_p = atcmd_output;
	year = month = day = hour = minute = second = 0;
	while (modif_p[0] != '\0') {
		value = atoi(modif_p);
		if (value == 0)
			modif_p++;
		else {
			if (modif_p[0] == '-' || modif_p[0] == '+')
				modif_p++;
			while (modif_p[0] >= '0' && modif_p[0] <= '9')
				modif_p++;
			if (modif_p[0] == 's') {
				second = value;
				modif_p++;
			} else if (modif_p[0] == 'n') {
				minute = value;
				modif_p++;
			} else if (modif_p[0] == 'm' && modif_p[1] == 'n') {
				minute = value;
				modif_p = modif_p + 2;
			} else if (modif_p[0] == 'h') {
				hour = value;
				modif_p++;
			} else if (modif_p[0] == 'd' || modif_p[0] == 'j') {
				day = value;
				modif_p++;
			} else if (modif_p[0] == 'm') {
				month = value;
				modif_p++;
			} else if (modif_p[0] == 'y' || modif_p[0] == 'a') {
				year = value;
				modif_p++;
			} else if (modif_p[0] != '\0') {
				modif_p++;
			}
		}
	}
	if (year == 0 && month == 0 && day == 0 && hour == 0 && minute == 0 && second == 0) {
		clif_displaymessage(fd, msg_txt(85)); //Tempo inválido para o comando ban.
		return -1;
	}

	chrif_char_ask_name(sd->status.account_id, atcmd_player_name, 2, year, month, day, hour, minute, second); // type: 2 - ban
	clif_displaymessage(fd, msg_txt(88)); //Nome do personagem enviado ao servidor de personagens (char-server).

	return 0;
}

/*==========================================
 * comando charunblock (uso: charunblock <nome_jogador>)
 *------------------------------------------*/
ACMD_FUNC(char_unblock)
{
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um jogador (uso: @charunblock <nome_jogador>).");
		return -1;
	}

	//Manda resposta ao login-server via char-server
   	chrif_char_ask_name(sd->status.account_id, atcmd_player_name, 3, 0, 0, 0, 0, 0, 0); // type: 3 - unblock
	clif_displaymessage(fd, msg_txt(88)); //Nome do personagem enviado ao servidor de personagens (char-server).

	return 0;
}

/*==========================================
 * comando charunban (uso: charunban <nome_jogador>)
 *------------------------------------------*/
ACMD_FUNC(char_unban)
{
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um jogador (uso: @charunban <nome_jogador>).");
		return -1;
	}

	//Manda resposta ao login-server via char-server
	chrif_char_ask_name(sd->status.account_id, atcmd_player_name, 4, 0, 0, 0, 0, 0, 0); // type: 4 - unban
	clif_displaymessage(fd, msg_txt(88)); //Nome do personagem enviado ao servidor de personagens (char-server).

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(night)
{
	nullpo_retr(-1, sd);

	if (night_flag != 1) {
		map_night_timer(night_timer_tid, 0, 0, 1);
	} else {
		clif_displaymessage(fd, msg_txt(89)); //Desculpe-me, mas já é noite. Impossível realizar este comando.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(day)
{
	nullpo_retr(-1, sd);

	if (night_flag != 0) {
		map_day_timer(day_timer_tid, 0, 0, 1);
	} else {
		clif_displaymessage(fd, msg_txt(90)); //Desculpe-me, mas já é dia. Impossível realizar este comando.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(doom)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;

	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (pl_sd->fd != fd && pc_isGM(sd) >= pc_isGM(pl_sd))
		{
			status_kill(&pl_sd->bl);
			clif_specialeffect(&pl_sd->bl,450,AREA);
			clif_displaymessage(pl_sd->fd, msg_txt(61)); //O mensageiro sagrado fez seu julgamento.
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(62)); //O julgamento foi feito.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(doommap)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;

	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (pl_sd->fd != fd && sd->bl.m == pl_sd->bl.m && pc_isGM(sd) >= pc_isGM(pl_sd))
		{
			status_kill(&pl_sd->bl);
			clif_specialeffect(&pl_sd->bl,450,AREA);
			clif_displaymessage(pl_sd->fd, msg_txt(61)); //O mensageiro sagrado fez seu julgamento.
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(62)); //O julgamento foi feito.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static void atcommand_raise_sub(struct map_session_data* sd)
{
	if (!status_isdead(&sd->bl))
		return;

	if(!status_revive(&sd->bl, 100, 100))
		return;
	clif_skill_nodamage(&sd->bl,&sd->bl,ALL_RESURRECTION,4,1);
	clif_displaymessage(sd->fd, msg_txt(63)); //O perdão foi mostrado.
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(raise)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
		
	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		atcommand_raise_sub(pl_sd);
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(64)); //O perdão foi concedido.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(raisemap)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;

	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		if (sd->bl.m == pl_sd->bl.m)
			atcommand_raise_sub(pl_sd);
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(64)); //O perdão foi concedido.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(kick)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um jogador (uso: @kick <nome/id>).");
		return -1;
	}

	if((pl_sd=map_nick2sd((char *)message)) == NULL && (pl_sd=map_charid2sd(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(3)); //Personagem não encontrado
		return -1;
	}

	if ( pc_isGM(sd) < pc_isGM(pl_sd) )
	{
		clif_displaymessage(fd, msg_txt(81)); //Seu nível de GM não te autoriza a fazer esta ação neste jogador.
		return -1;
	}
	
	clif_GM_kick(sd, pl_sd);
	
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(kickall)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (pc_isGM(sd) >= pc_isGM(pl_sd)) { //Você só pode kickar jogadores de mesmo GM-level ou abaixo.
			if (sd->status.account_id != pl_sd->status.account_id)
				clif_GM_kick(NULL, pl_sd);
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(195)); //Todos os jogadores foram desconectados do servidor!

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(allskill)
{
	nullpo_retr(-1, sd);
	pc_allskillup(sd); //Todas as habilidades
	sd->status.skill_point = 0; // 0 pontos de skill.
	clif_updatestatus(sd, SP_SKILLPOINT); //atualização
	clif_displaymessage(fd, msg_txt(76)); //Você agora possui todas as habilidades.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(questskill)
{
	int skill_id;
	nullpo_retr(-1, sd);

	if (!message || !*message || (skill_id = atoi(message)) < 0) {
		clif_displaymessage(fd, "Por favor, entre com com o número de uma habilidade de quest (uso: @questskill <#:0+>).");
		return -1;
	}
	if (skill_id < 0 && skill_id >= MAX_SKILL_DB) {
		clif_displaymessage(fd, msg_txt(198)); //O número desta habilidade não existe.
		return -1;
	}
	if (!(skill_get_inf2(skill_id) & INF2_QUEST_SKILL)) {
		clif_displaymessage(fd, msg_txt(197)); //O número dessa habilidade não exite ou essa não é uma habilidade de quest.
		return -1;
	}
	if (pc_checkskill(sd, skill_id) > 0) {
		clif_displaymessage(fd, msg_txt(196)); //Você já tem essa habilidade de quest.
		return -1;
	}

	pc_skill(sd, skill_id, 1, 0);
	clif_displaymessage(fd, msg_txt(70)); //Você aprendeu a habilidade.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(lostskill)
{
	int skill_id;
	nullpo_retr(-1, sd);

	if (!message || !*message || (skill_id = atoi(message)) < 0) {
		clif_displaymessage(fd, "Por favor, entre com o número de uma habilidade de quest (uso: @lostskill <#:0+>).");
		return -1;
	}
	if (skill_id < 0 && skill_id >= MAX_SKILL) {
		clif_displaymessage(fd, msg_txt(198)); // O número desta habilidade não existe.
		return -1;
	}
	if (!(skill_get_inf2(skill_id) & INF2_QUEST_SKILL)) {
		clif_displaymessage(fd, msg_txt(197)); // O número dessa habilidade não exite ou essa não é uma habilidade de quest.
		return -1;
	}
	if (pc_checkskill(sd, skill_id) == 0) {
		clif_displaymessage(fd, msg_txt(201)); //Você já tem essa habilidade de quest.
		return -1;
	}

	sd->status.skill[skill_id].lv = 0;
	sd->status.skill[skill_id].flag = 0;
	clif_deleteskill(sd,skill_id);
	clif_displaymessage(fd, msg_txt(71)); //Você esqueceu a habilidade.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(spiritball)
{
	int max_spiritballs = min(ARRAYLENGTH(sd->spirit_timer), 0x7FFF);
	int number;
	nullpo_retr(-1, sd);

	if( !message || !*message || (number = atoi(message)) < 0 || number > max_spiritballs )
	{
		char msg[CHAT_SIZE_MAX];
		safesnprintf(msg, sizeof(msg), "Uso: @spiritball <número: 0-%d>", max_spiritballs);
		clif_displaymessage(fd, msg);
		return -1;
	}

	if( sd->spiritball > 0 )
		pc_delspiritball(sd, sd->spiritball, 1);
	sd->spiritball = number;
	clif_spiritball(sd);
	// sem mensagem, o jogador pode notar a diferença.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(party)
{
	char party[NAME_LENGTH];
	nullpo_retr(-1, sd);

	memset(party, '\0', sizeof(party));

	if (!message || !*message || sscanf(message, "%23[^\n]", party) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um grupo (uso: @party <nome_grupo>).");
		return -1;
	}

	party_create(sd, party, 0, 0);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(guild)
{
	char guild[NAME_LENGTH];
	int prev;
	nullpo_retr(-1, sd);

	memset(guild, '\0', sizeof(guild));

	if (!message || !*message || sscanf(message, "%23[^\n]", guild) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um clã (uso: @guild <nome_clã>).");
		return -1;
	}

	prev = battle_config.guild_emperium_check;
	battle_config.guild_emperium_check = 0;
	guild_create(sd, guild);
	battle_config.guild_emperium_check = prev;

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(agitstart)
{
	nullpo_retr(-1, sd);
	if (agit_flag == 1) {
		clif_displaymessage(fd, msg_txt(73)); //A Guerra do Emperium já começou.
		return -1;
	}

	agit_flag = 1;
	guild_agit_start();
	clif_displaymessage(fd, msg_txt(72)); //A Guerra do Emperium começou!

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(agitstart2)
{
	nullpo_retr(-1, sd);
	if (agit2_flag == 1) {
		clif_displaymessage(fd, msg_txt(404)); //A Guerra do Emperium SE já começou.
		return -1;
	}

	agit2_flag = 1;
	guild_agit2_start();
	clif_displaymessage(fd, msg_txt(403)); //A Guerra do Emperium SE começou!

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(agitend)
{
	nullpo_retr(-1, sd);
	if (agit_flag == 0) {
		clif_displaymessage(fd, msg_txt(75)); //A Guerra de Emperium já não está em progresso.
		return -1;
	}

	agit_flag = 0;
	guild_agit_end();
	clif_displaymessage(fd, msg_txt(74)); //A Guerra do Emperium terminou!

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(agitend2)
{
	nullpo_retr(-1, sd);
	if (agit2_flag == 0) {
		clif_displaymessage(fd, msg_txt(406)); //A Guerra do Emperium SE já não está em progresso.
		return -1;
	}

	agit2_flag = 0;
	guild_agit2_end();
	clif_displaymessage(fd, msg_txt(405)); //A Guerra do Emperium SE terminou!

	return 0;
}

/*==========================================
 * @mapexit - desliga o map-server
 *------------------------------------------*/
ACMD_FUNC(mapexit)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;

	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		if (sd->status.account_id != pl_sd->status.account_id)
			clif_GM_kick(NULL, pl_sd);
	mapit_free(iter);

	clif_GM_kick(NULL, sd);
	
	flush_fifos();

	runflag = 0;

	return 0;
}

/*==========================================
 * idsearch <parte_do_nome>: reescrito por [Yor]
 *------------------------------------------*/
ACMD_FUNC(idsearch)
{
	char item_name[100];
	unsigned int i, match;
	struct item_data *item_array[MAX_SEARCH];
	nullpo_retr(-1, sd);

	memset(item_name, '\0', sizeof(item_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%99s", item_name) < 0) {
		clif_displaymessage(fd, "Por favor, entre com a parte do nome de um item (uso: @idsearch <parte_nome_do_item>).");
		return -1;
	}

	sprintf(atcmd_output, msg_txt(77), item_name); //O resultado da referência é '%s' (nome: id):
	clif_displaymessage(fd, atcmd_output);
	match = itemdb_searchname_array(item_array, MAX_SEARCH, item_name);
	if (match > MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, match);
		clif_displaymessage(fd, atcmd_output);
		match = MAX_SEARCH;
	}
	for(i = 0; i < match; i++) {
		sprintf(atcmd_output, msg_txt(78), item_array[i]->jname, item_array[i]->nameid); // %s: %d
		clif_displaymessage(fd, atcmd_output);
	}
	sprintf(atcmd_output, msg_txt(79), match); // %d encontrado acima.
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * Teleporta todos os jogadores online para a sua localização
 *------------------------------------------*/
ACMD_FUNC(recallall)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	int count;
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(204));
		return -1;
	}

	count = 0;
	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (sd->status.account_id != pl_sd->status.account_id && pc_isGM(sd) >= pc_isGM(pl_sd))
		{
			if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd))
				count++;
			else {
				if (pc_isdead(pl_sd)) { //Acorda eles.
					pc_setstand(pl_sd);
					pc_setrestartvalue(pl_sd,1);
				}
				pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_RESPAWN);
			}
		}
	}
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(92)); //Todos os personagens foram recolhidos!
	if (count) {
		sprintf(atcmd_output, "Já que você não está autorizado a teleportar de alguns mapas, o(s) jogador(es)%d não foram recolhidos.", count);
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Teleporta todos os jogadores online de um clã para a sua localização.
 *------------------------------------------*/
ACMD_FUNC(guildrecall)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	int count;
	char guild_name[NAME_LENGTH];
	struct guild *g;
	nullpo_retr(-1, sd);

	memset(guild_name, '\0', sizeof(guild_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%23[^\n]", guild_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com um nome/id de um clã (uso: @guildrecall <nome/id>).");
		return -1;
	}

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(204)); //Você não está autorizado a teletransportar alguém para seu mapa atual.
		return -1;
	}

	if ((g = guild_searchname(guild_name)) == NULL && //nome primeiro para evitar nomes começados com números
	    (g = guild_search(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(94)); //Nome/ID incorreto, ou nenhum personagem do clã está conectado.
		return -1;
	}

	count = 0;

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (sd->status.account_id != pl_sd->status.account_id && pl_sd->status.guild_id == g->guild_id)
		{
			if (pc_isGM(pl_sd) > pc_isGM(sd))
				continue; //Pula GMs de nível maior que o seu.
			if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd))
				count++;
			else
				pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_RESPAWN);
		}
	}
	mapit_free(iter);

	sprintf(atcmd_output, msg_txt(93), g->name); //Todos os personagens online do clã %s foram recolhidos.
	clif_displaymessage(fd, atcmd_output);
	if (count) {
		sprintf(atcmd_output, "Já que você não está autorizado a teleportar de alguns mapas, o(s) jogador(es)%d não foram recolhidos.", count);
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Teleporta todos os membros de um grupo para a sua localização.
 *------------------------------------------*/
ACMD_FUNC(partyrecall)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	char party_name[NAME_LENGTH];
	struct party_data *p;
	int count;
	nullpo_retr(-1, sd);

	memset(party_name, '\0', sizeof(party_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message || sscanf(message, "%23[^\n]", party_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome/id de um grupo (uso: @partyrecall <nome/id>).");
		return -1;
	}

	if (sd->bl.m >= 0 && map[sd->bl.m].flag.nowarpto && battle_config.any_warp_GM_min_level > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(204)); //Você não está autorizado a teletransportar alguém para seu mapa atual.
		return -1;
	}

	if ((p = party_searchname(party_name)) == NULL && //nome primeiro para evitar nomes começados com números.
	    (p = party_search(atoi(message))) == NULL)
	{
		clif_displaymessage(fd, msg_txt(96)); //Nome/ID incorreto, ou nenhum personagem do grupo está conectado.
		return -1;
	}

	count = 0;

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if (sd->status.account_id != pl_sd->status.account_id && pl_sd->status.party_id == p->party.party_id)
		{
			if (pc_isGM(pl_sd) > pc_isGM(sd))
				continue; //Pula GMs de nível maior que o seu.
			if (pl_sd->bl.m >= 0 && map[pl_sd->bl.m].flag.nowarp && battle_config.any_warp_GM_min_level > pc_isGM(sd))
				count++;
			else
				pc_setpos(pl_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_RESPAWN);
		}
	}
	mapit_free(iter);

	sprintf(atcmd_output, msg_txt(95), p->party.name); // All online characters of the %s party have been recalled to your position.
	clif_displaymessage(fd, atcmd_output);
	if (count) {
		sprintf(atcmd_output, "Já que você não está autorizado a teleportar de alguns mapas, o(s) jogador(es)%d não foram recolhidos.", count);
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(reloaditemdb)
{
	nullpo_retr(-1, sd);
	itemdb_reload();
	clif_displaymessage(fd, msg_txt(97)); //Database de itens recarregada.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(reloadmobdb)
{
	nullpo_retr(-1, sd);
	mob_reload();
	read_petdb();
	merc_reload();
	reload_elementaldb();
	clif_displaymessage(fd, msg_txt(98)); //Database de monstros recarregada.

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(reloadskilldb)
{
	nullpo_retr(-1, sd);
	skill_reload();
	merc_skill_reload();
	reload_elemental_skilldb();
	clif_displaymessage(fd, msg_txt(99)); //Database de habilidades recarregada.

	return 0;
}

/*==========================================
 * @reloadatcommand - carrega atcommand_athena.conf
 *------------------------------------------*/
ACMD_FUNC(reloadatcommand)
{
	atcommand_config_read(ATCOMMAND_CONF_FILENAME);
	clif_displaymessage(fd, msg_txt(254));
	return 0;
}
/*==========================================
 * @reloadbattleconf - carrega battle_athena.conf
 *------------------------------------------*/
ACMD_FUNC(reloadbattleconf)
{
	struct Battle_Config prev_config;
	memcpy(&prev_config, &battle_config, sizeof(prev_config));

	battle_config_read(BATTLE_CONF_FILENAME);

	if( prev_config.item_rate_mvp          != battle_config.item_rate_mvp
	||  prev_config.item_rate_common       != battle_config.item_rate_common
	||  prev_config.item_rate_common_boss  != battle_config.item_rate_common_boss
	||  prev_config.item_rate_card         != battle_config.item_rate_card
	||  prev_config.item_rate_card_boss    != battle_config.item_rate_card_boss
	||  prev_config.item_rate_equip        != battle_config.item_rate_equip
	||  prev_config.item_rate_equip_boss   != battle_config.item_rate_equip_boss
	||  prev_config.item_rate_heal         != battle_config.item_rate_heal
	||  prev_config.item_rate_heal_boss    != battle_config.item_rate_heal_boss
	||  prev_config.item_rate_use          != battle_config.item_rate_use
	||  prev_config.item_rate_use_boss     != battle_config.item_rate_use_boss
	||  prev_config.item_rate_treasure     != battle_config.item_rate_treasure
	||  prev_config.item_rate_adddrop      != battle_config.item_rate_adddrop
	||  prev_config.logarithmic_drops      != battle_config.logarithmic_drops
	||  prev_config.item_drop_common_min   != battle_config.item_drop_common_min
	||  prev_config.item_drop_common_max   != battle_config.item_drop_common_max
	||  prev_config.item_drop_card_min     != battle_config.item_drop_card_min
	||  prev_config.item_drop_card_max     != battle_config.item_drop_card_max
	||  prev_config.item_drop_equip_min    != battle_config.item_drop_equip_min
	||  prev_config.item_drop_equip_max    != battle_config.item_drop_equip_max
	||  prev_config.item_drop_mvp_min      != battle_config.item_drop_mvp_min
	||  prev_config.item_drop_mvp_max      != battle_config.item_drop_mvp_max
	||  prev_config.item_drop_heal_min     != battle_config.item_drop_heal_min
	||  prev_config.item_drop_heal_max     != battle_config.item_drop_heal_max
	||  prev_config.item_drop_use_min      != battle_config.item_drop_use_min
	||  prev_config.item_drop_use_max      != battle_config.item_drop_use_max
	||  prev_config.item_drop_treasure_min != battle_config.item_drop_treasure_min
	||  prev_config.item_drop_treasure_max != battle_config.item_drop_treasure_max
	||  prev_config.base_exp_rate          != battle_config.base_exp_rate
	||  prev_config.job_exp_rate           != battle_config.job_exp_rate
	)
  	{	// Rates de drop ou exp alteradas.
		mob_reload(); 
#ifndef TXT_ONLY
		chrif_ragsrvinfo(battle_config.base_exp_rate, battle_config.job_exp_rate, battle_config.item_rate_common);
#endif
	}
	clif_displaymessage(fd, msg_txt(255));
	return 0;
}
/*==========================================
 * @reloadstatusdb - carrega job_db1.txt job_db2.txt job_db2-2.txt refine_db.txt size_fix.txt
 *------------------------------------------*/
ACMD_FUNC(reloadstatusdb)
{
	status_readdb();
	clif_displaymessage(fd, msg_txt(256));
	return 0;
}
/*==========================================
 * @reloadpcdb - carrega exp.txt skill_tree.txt attr_fix.txt statpoint.txt
 *------------------------------------------*/
ACMD_FUNC(reloadpcdb)
{
	pc_readdb();
	clif_displaymessage(fd, msg_txt(257));
	return 0;
}

/*==========================================
 * @reloadmotd - carrega motd.txt
 *------------------------------------------*/
ACMD_FUNC(reloadmotd)
{
	pc_read_motd();
	clif_displaymessage(fd, msg_txt(268));
	return 0;
}

/*==========================================
 * @reloadscript - carrega todos os scripts (npcs, warps, mob spawns, ...)
 *------------------------------------------*/
ACMD_FUNC(reloadscript)
{
	nullpo_retr(-1, sd);
	//atcommand_broadcast( fd, sd, "@broadcast", "eAthena Server is Rehashing..." );
	//atcommand_broadcast( fd, sd, "@broadcast", "You will feel a bit of lag at this point !" );
	//atcommand_broadcast( fd, sd, "@broadcast", "Reloading NPCs..." );

	flush_fifos();
	script_reload();
	npc_reload();

	clif_displaymessage(fd, msg_txt(100)); //Scripts foram carregados.

	return 0;
}

/*==========================================
 * @mapinfo [0-3] <nome do mapa> por MC_Cameri
 * => Mostra informações sobre o mapa [nome do mapa]
 * 0 = sem informação adicional
 * 1 = Mostra os usuários nesse mapa e suas localizações
 * 2 = Mostra os NPCs no mapa.
 * 3 = Mostra as lojas/chats no mapa (não implementado)
 *------------------------------------------*/
ACMD_FUNC(mapinfo)
{
	struct map_session_data* pl_sd;
	struct s_mapiterator* iter;
	struct npc_data *nd = NULL;
	struct chat_data *cd = NULL;
	char direction[12];
	int i, m_id, chat_num, list = 0;
	unsigned short m_index;
	char mapname[24];

	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(mapname, '\0', sizeof(mapname));
	memset(direction, '\0', sizeof(direction));

	sscanf(message, "%d %23[^\n]", &list, mapname);

	if (list < 0 || list > 3) {
		clif_displaymessage(fd, "Por favor, entre com pelo menos um número válido (uso: @mapinfo <0-3> [mapa]).");
		return -1;
	}

	if (mapname[0] == '\0') {
		safestrncpy(mapname, mapindex_id2name(sd->mapindex), MAP_NAME_LENGTH);
		m_id =  map_mapindex2mapid(sd->mapindex);
	} else {
		m_id = map_mapname2mapid(mapname);
	}

	if (m_id < 0) {
		clif_displaymessage(fd, msg_txt(1)); // Mapa não encontrado.
		return -1;
	}
	m_index = mapindex_name2id(mapname); //Este não deve falhar desde que a procura anterior não falhou.
	
	clif_displaymessage(fd, "------ Informações do mapa ------");

	// count chats (for initial message)
	chat_num = 0;
	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		if( (cd = (struct chat_data*)map_id2bl(pl_sd->chatID)) != NULL && pl_sd->mapindex == m_index && cd->usersd[0] == pl_sd )
			chat_num++;
	mapit_free(iter);

	sprintf(atcmd_output, "Nome do mapa: %s | Jogadores no mapa: %d | NPCs no mapa: %d | Chats no mapa: %d", mapname, map[m_id].users, map[m_id].npc_num, chat_num);
	clif_displaymessage(fd, atcmd_output);
	clif_displaymessage(fd, "------ Flags do mapa ------");
	if (map[m_id].flag.town)
		clif_displaymessage(fd, "Mapa de cidade");

	if (battle_config.autotrade_mapflag == map[m_id].flag.autotrade)
		clif_displaymessage(fd, "Autotrade habilitado");
	else
		clif_displaymessage(fd, "Autotrade desabilitado");
	
	if (map[m_id].flag.battleground)
		clif_displaymessage(fd, "Battlegrounds ligados.");
		
	strcpy(atcmd_output,"PvP Flags: ");
	if (map[m_id].flag.pvp)
		strcat(atcmd_output, "Pvp ON | ");
	if (map[m_id].flag.pvp_noguild)
		strcat(atcmd_output, "NoGuild | ");
	if (map[m_id].flag.pvp_noparty)
		strcat(atcmd_output, "NoParty | ");
	if (map[m_id].flag.pvp_nightmaredrop)
		strcat(atcmd_output, "NightmareDrop | ");
	if (map[m_id].flag.pvp_nocalcrank)
		strcat(atcmd_output, "NoCalcRank | ");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,"GvG Flags: ");
	if (map[m_id].flag.gvg)
		strcat(atcmd_output, "GvG ON | ");
	if (map[m_id].flag.gvg_dungeon)
		strcat(atcmd_output, "GvG Dungeon | ");
	if (map[m_id].flag.gvg_castle)
		strcat(atcmd_output, "GvG Castle | ");
	if (map[m_id].flag.gvg_noparty)
		strcat(atcmd_output, "NoParty | ");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,"Teleport Flags: ");
	if (map[m_id].flag.noteleport)
		strcat(atcmd_output, "NoTeleport | ");
	if (map[m_id].flag.monster_noteleport)
		strcat(atcmd_output, "Monster NoTeleport | ");
	if (map[m_id].flag.nowarp)
		strcat(atcmd_output, "NoWarp | ");
	if (map[m_id].flag.nowarpto)
		strcat(atcmd_output, "NoWarpTo | ");
	if (map[m_id].flag.noreturn)
		strcat(atcmd_output, "NoReturn | ");
	if (map[m_id].flag.nogo)
		strcat(atcmd_output, "NoGo | ");
	if (map[m_id].flag.nomemo)
		strcat(atcmd_output, "NoMemo | ");
	clif_displaymessage(fd, atcmd_output);

	sprintf(atcmd_output, "Sem penalidade de exp: %s | Sem penalidades de zeny: %s", (map[m_id].flag.noexppenalty) ? "On" : "Off", (map[m_id].flag.nozenypenalty) ? "On" : "Off");
	clif_displaymessage(fd, atcmd_output);

	if (map[m_id].flag.nosave) {
		if (!map[m_id].save.map)
			sprintf(atcmd_output, "Sem salve. (Retorna para o último ponto de salve.)");
		else if (map[m_id].save.x == -1 || map[m_id].save.y == -1 )
			sprintf(atcmd_output, "Sem retorno, ponto de retorno: %s,Aleatório",mapindex_id2name(map[m_id].save.map));
		else
			sprintf(atcmd_output, "Sem retorno, ponto de retorno: %s,%d,%d",
				mapindex_id2name(map[m_id].save.map),map[m_id].save.x,map[m_id].save.y);
		clif_displaymessage(fd, atcmd_output);
	}

	strcpy(atcmd_output,"Weather Flags: ");
	if (map[m_id].flag.snow)
		strcat(atcmd_output, "Snow | ");
	if (map[m_id].flag.fog)
		strcat(atcmd_output, "Fog | ");
	if (map[m_id].flag.sakura)
		strcat(atcmd_output, "Sakura | ");
	if (map[m_id].flag.clouds)
		strcat(atcmd_output, "Clouds | ");
	if (map[m_id].flag.clouds2)
		strcat(atcmd_output, "Clouds2 | ");
	if (map[m_id].flag.fireworks)
		strcat(atcmd_output, "Fireworks | ");
	if (map[m_id].flag.leaves)
		strcat(atcmd_output, "Leaves | ");
	if (map[m_id].flag.rain)
		strcat(atcmd_output, "Rain | ");
	if (map[m_id].flag.nightenabled)
		strcat(atcmd_output, "Displays Night | ");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,"Other Flags: ");
	if (map[m_id].flag.nobranch)
		strcat(atcmd_output, "NoBranch | ");
	if (map[m_id].flag.notrade)
		strcat(atcmd_output, "NoTrade | ");
	if (map[m_id].flag.novending)
		strcat(atcmd_output, "NoVending | ");
	if (map[m_id].flag.nodrop)
		strcat(atcmd_output, "NoDrop | ");
	if (map[m_id].flag.noskill)
		strcat(atcmd_output, "NoSkill | ");
	if (map[m_id].flag.noicewall)
		strcat(atcmd_output, "NoIcewall | ");
	if (map[m_id].flag.allowks)
		strcat(atcmd_output, "AllowKS | ");
	if (map[m_id].flag.reset)
		strcat(atcmd_output, "Reset | ");
	clif_displaymessage(fd, atcmd_output);

	strcpy(atcmd_output,"Other Flags: ");
	if (map[m_id].nocommand)
		strcat(atcmd_output, "NoCommand | ");
	if (map[m_id].flag.nobaseexp)
		strcat(atcmd_output, "NoBaseEXP | ");
	if (map[m_id].flag.nojobexp)
		strcat(atcmd_output, "NoJobEXP | ");
	if (map[m_id].flag.nomobloot)
		strcat(atcmd_output, "NoMobLoot | ");
	if (map[m_id].flag.nomvploot)
		strcat(atcmd_output, "NoMVPLoot | ");
	if (map[m_id].flag.partylock)
		strcat(atcmd_output, "PartyLock | ");
	if (map[m_id].flag.guildlock)
		strcat(atcmd_output, "GuildLock | ");
	clif_displaymessage(fd, atcmd_output);

	switch (list) {
	case 0:
		//Não faz nada. Lista 0, não mostra nada adicional..
		break;
	case 1:
		clif_displaymessage(fd, "----- Jogadores no mapa -----");
		iter = mapit_getallusers();
		for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		{
			if (pl_sd->mapindex == m_index) {
				sprintf(atcmd_output, "Jogador '%s' (sessão #%d) | Localização: %d,%d",
				        pl_sd->status.name, pl_sd->fd, pl_sd->bl.x, pl_sd->bl.y);
				clif_displaymessage(fd, atcmd_output);
			}
		}
		mapit_free(iter);
		break;
	case 2:
		clif_displaymessage(fd, "----- NPCs no mapa -----");
		for (i = 0; i < map[m_id].npc_num;)
		{
			nd = map[m_id].npc[i];
			switch(nd->ud.dir) {
			case 0:  strcpy(direction, "Norte"); break;
			case 1:  strcpy(direction, "Noroeste"); break;
			case 2:  strcpy(direction, "Oeste"); break;
			case 3:  strcpy(direction, "Sudoeste"); break;
			case 4:  strcpy(direction, "Sul"); break;
			case 5:  strcpy(direction, "Sudeste"); break;
			case 6:  strcpy(direction, "Leste"); break;
			case 7:  strcpy(direction, "Nordeste"); break;
			case 9:  strcpy(direction, "Norte"); break;
			default: strcpy(direction, "Desconhecido."); break;
			}
			sprintf(atcmd_output, "NPC %d: %s | Direção: %s | Sprite: %d | Localização: %d %d",
			        ++i, nd->name, direction, nd->class_, nd->bl.x, nd->bl.y);
			clif_displaymessage(fd, atcmd_output);
		}
		break;
	case 3:
		clif_displaymessage(fd, "----- Chats no mapa -----");
		iter = mapit_getallusers();
		for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		{
			if ((cd = (struct chat_data*)map_id2bl(pl_sd->chatID)) != NULL &&
			    pl_sd->mapindex == m_index &&
			    cd->usersd[0] == pl_sd)
			{
				sprintf(atcmd_output, "Chat: %s | Jogador: %s | Localização: %d %d",
				        cd->title, pl_sd->status.name, cd->bl.x, cd->bl.y);
				clif_displaymessage(fd, atcmd_output);
				sprintf(atcmd_output, "   Usuários: %d/%d | Senha: %s | Público: %s",
				        cd->users, cd->limit, cd->pass, (cd->pub) ? "Sim" : "Não");
				clif_displaymessage(fd, atcmd_output);
			}
		}
		mapit_free(iter);
		break;
	default: //normalmente impossível chegar aqui.
		clif_displaymessage(fd, "Por favor, entre com pelo menos um número válido (uso: @mapinfo <0-3> [mapa]).");
		return -1;
		break;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(mount)
{
	int msg[4] = { 0, 0, 0, 0 }, option = 0, skillnum = 0, val, riding_flag = 0;
	nullpo_retr(-1, sd);

	if( !message || !*message || sscanf(message, "%d", &val) < 1 || val < 1 || val > 5 )
		val = 0; //Cor padrão do peco dragão

	if( (sd->class_&MAPID_UPPERMASK) == MAPID_KNIGHT || (sd->class_&MAPID_UPPERMASK) == MAPID_CRUSADER )
	{
		if( sd->class_&JOBL_THIRD )
		{
			if( (sd->class_&MAPID_UPPERMASK) == MAPID_KNIGHT )
			{ //Cavaleiro Rúnico
				if( pc_isriding(sd,OPTION_RIDING_DRAGON) )
					riding_flag = 1;
				msg[0] = 700; msg[1] = 702; msg[2] = 701; msg[3] = 703;
				option = pc_isriding(sd,OPTION_RIDING_DRAGON) ? OPTION_RIDING_DRAGON :
					(val == 2) ? OPTION_BLACK_DRAGON :
					(val == 3) ? OPTION_WHITE_DRAGON :
					(val == 4) ? OPTION_BLUE_DRAGON :
					(val == 5) ? OPTION_RED_DRAGON :
					OPTION_GREEN_DRAGON;
				skillnum = RK_DRAGONTRAINING;
			}
			else
			{ //Guarda real
				if( pc_isriding(sd,OPTION_RIDING) )
					riding_flag = 1;
				msg[0] = 714; msg[1] = 716; msg[2] = 715; msg[3] = 717;
				option = OPTION_RIDING;
				skillnum = KN_RIDING;
			}
		}
		else
		{ // Lorde - Cavaleiro - Paladino - Templário
			if( pc_isriding(sd,OPTION_RIDING) )
				riding_flag = 1;
			msg[0] = 102; msg[1] = 214; msg[2] = 213; msg[3] = 212;
			option = OPTION_RIDING;
			skillnum = KN_RIDING;
		}
	}
	else if( sd->class_&JOBL_THIRD )
	{
		if( (sd->class_&MAPID_UPPERMASK) == MAPID_HUNTER )
		{ //Sentinela
			if( pc_iswarg(sd) )
				pc_setoption(sd,sd->sc.option&~OPTION_WUG);
			if( pc_isriding(sd,OPTION_RIDING_WUG) )
				riding_flag = 1;
			msg[0] = 704; msg[1] = 706; msg[2] = 705; msg[3] = 707;
			option = OPTION_RIDING_WUG;
			skillnum = RA_WUGRIDER;
		}
		else if( (sd->class_&MAPID_UPPERMASK) == MAPID_BLACKSMITH )
		{
			if( pc_isriding(sd, OPTION_MADO) )
				riding_flag = 1;
			msg[0] = 710; msg[1] = 712; msg[2] = 711; msg[3] = 713;
			option = OPTION_MADO;
		}
	}

	if( !option )
	{
		clif_displaymessage(fd, "Você não pode montar com sua classe atual.");
		return -1;
	}

	if( skillnum && !pc_checkskill(sd,skillnum) )
	{ //Você não tem a skill para montar.
		clif_displaymessage(fd,"Você não pode montar com sua classe atual.");
		return -1;
	}
	if( sd->disguise )
	{ // Disguised
		clif_displaymessage(fd, msg_txt(msg[3])); //Você não pode montar transformado..
		return -1;
	}
	if( riding_flag )
	{ //Dismount
		pc_setoption(sd, sd->sc.option & ~option);
		if( option == OPTION_RIDING_WUG )
			pc_setoption(sd, sd->sc.option&OPTION_WUG);
		clif_displaymessage(fd, msg_txt(msg[1])); //Você soltou sua montaria.
	}
	else
	{ // Mount
		pc_setoption(sd, sd->sc.option | option);
		clif_displaymessage(fd, msg_txt(msg[0])); //Você montou.
	}

	return 0;
}

/*==========================================
 *Comando espião por Syrus22
 *------------------------------------------*/
ACMD_FUNC(guildspy)
{
	char guild_name[NAME_LENGTH];
	struct guild *g;
	nullpo_retr(-1, sd);

	memset(guild_name, '\0', sizeof(guild_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!enable_spy)
	{
		clif_displaymessage(fd, "O mapserver não tem suporte para o comando espião.");
		return -1;
	}
	if (!message || !*message || sscanf(message, "%23[^\n]", guild_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome/id do clã (uso: @guildspy <nome/id>).");
		return -1;
	}

	if ((g = guild_searchname(guild_name)) != NULL || //nome primeiro para evitar erro quando o nome começa com número
           (g = guild_search(atoi(message))) != NULL) {
		if (sd->guildspy == g->guild_id) {
			sd->guildspy = 0;
			sprintf(atcmd_output, msg_txt(103), g->name); //Não há mais um espião no clã %s.
			clif_displaymessage(fd, atcmd_output);
		} else {
			sd->guildspy = g->guild_id;
			sprintf(atcmd_output, msg_txt(104), g->name); //Espiando o clã %s.
			clif_displaymessage(fd, atcmd_output);
		}
	} else {
		clif_displaymessage(fd, msg_txt(94)); //Nome/ID incorreto, ou nenhum personagem do clã está conectado.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(partyspy)
{
	char party_name[NAME_LENGTH];
	struct party_data *p;
	nullpo_retr(-1, sd);

	memset(party_name, '\0', sizeof(party_name));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!enable_spy)
	{
		clif_displaymessage(fd, "O mapserver não tem suporte para o comando espião.");
		return -1;
	}

	if (!message || !*message || sscanf(message, "%23[^\n]", party_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome/id de um grupo (uso: @partyspy <nome/id do grupo>).");
		return -1;
	}

	if ((p = party_searchname(party_name)) != NULL || //nome primeiro para evitar erro quando o nome começa com número
	    (p = party_search(atoi(message))) != NULL) {
		if (sd->partyspy == p->party.party_id) {
			sd->partyspy = 0;
			sprintf(atcmd_output, msg_txt(105), p->party.name); //Não há mais um espião no clã %s.
			clif_displaymessage(fd, atcmd_output);
		} else {
			sd->partyspy = p->party.party_id;
			sprintf(atcmd_output, msg_txt(106), p->party.name); //Espiando o clã %s.
			clif_displaymessage(fd, atcmd_output);
		}
	} else {
		clif_displaymessage(fd, msg_txt(96)); //Nome/ID incorreto, ou nenhum personagem do clã está conectado.
		return -1;
	}

	return 0;
}

/*==========================================
 * @repairall [Valaris]
 *------------------------------------------*/
ACMD_FUNC(repairall)
{
	int count, i;
	nullpo_retr(-1, sd);

	count = 0;
	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->status.inventory[i].nameid && sd->status.inventory[i].attribute == 1) {
			sd->status.inventory[i].attribute = 0;
			clif_produceeffect(sd, 0, sd->status.inventory[i].nameid);
			count++;
		}
	}

	if (count > 0) {
		clif_misceffect(&sd->bl, 3);
		clif_equiplist(sd);
		clif_displaymessage(fd, msg_txt(107)); //Todos os itens foram consertados.
	} else {
		clif_displaymessage(fd, msg_txt(108)); //Nenhum item precisa de conserto.
		return -1;
	}

	return 0;
}

/*==========================================
 * @nuke [Valaris]
 *------------------------------------------*/
ACMD_FUNC(nuke)
{
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um jogador (uso: @nuke <nome_personagem>).");
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) != NULL) {
		if (pc_isGM(sd) >= pc_isGM(pl_sd)) { //você só pode matar alguém do seu nível GM ou abaixo.
			skill_castend_nodamage_id(&pl_sd->bl, &pl_sd->bl, NPC_SELFDESTRUCTION, 99, gettick(), 0);
			clif_displaymessage(fd, msg_txt(109)); //Jogador aniquilado!
		} else {
			clif_displaymessage(fd, msg_txt(81)); //Seu nível de GM não te autoriza a fazer esta ação neste jogador.
			return -1;
		}
	} else {
		clif_displaymessage(fd, msg_txt(3)); //Personagem não encontrado.
		return -1;
	}

	return 0;
}

/*==========================================
 * @tonpc
 *------------------------------------------*/
ACMD_FUNC(tonpc)
{
	char npcname[NAME_LENGTH+1];
	struct npc_data *nd;

	nullpo_retr(-1, sd);

	memset(npcname, 0, sizeof(npcname));

	if (!message || !*message || sscanf(message, "%23[^\n]", npcname) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um NPC (uso: @tonpc <nome_NPC>).");
		return -1;
	}

	if ((nd = npc_name2id(npcname)) != NULL) {
		if (pc_setpos(sd, map_id2index(nd->bl.m), nd->bl.x, nd->bl.y, CLR_TELEPORT) == 0)
			clif_displaymessage(fd, msg_txt(0)); //Teletransportado.
		else
			return -1;
	} else {
		clif_displaymessage(fd, msg_txt(111)); //Este NPC não existe.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(shownpc)
{
	char NPCname[NAME_LENGTH+1];
	nullpo_retr(-1, sd);

	memset(NPCname, '\0', sizeof(NPCname));

	if (!message || !*message || sscanf(message, "%23[^\n]", NPCname) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um NPC (uso: @enablenpc <nome_NPC>).");
		return -1;
	}

	if (npc_name2id(NPCname) != NULL) {
		npc_enable(NPCname, 1);
		clif_displaymessage(fd, msg_txt(110)); // Npc ativado.
	} else {
		clif_displaymessage(fd, msg_txt(111)); // Este NPC não existe.
		return -1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(hidenpc)
{
	char NPCname[NAME_LENGTH+1];
	nullpo_retr(-1, sd);

	memset(NPCname, '\0', sizeof(NPCname));

	if (!message || !*message || sscanf(message, "%23[^\n]", NPCname) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um NPC (uso: @hidenpc <nome_NPC>).");
		return -1;
	}

	if (npc_name2id(NPCname) == NULL) {
		clif_displaymessage(fd, msg_txt(111)); // Este NPC não existe.
		return -1;
	}

	npc_enable(NPCname, 0);
	clif_displaymessage(fd, msg_txt(112)); // NPC desativado.
	return 0;
}

ACMD_FUNC(loadnpc)
{
	FILE *fp;

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre a com o nome de uma arquivo de script. (uso: @loadnpc <nome_arquivo>).");
		return -1;
	}

	// checa se o arquivo existe
	if ((fp = fopen(message, "r")) == NULL) {
		clif_displaymessage(fd, msg_txt(261));
		return -1;
	}
	fclose(fp);

	// adiciona na lista de scripts na source e o executa.
	npc_addsrcfile(message);
	npc_parsesrcfile(message);
	npc_read_event_script();

	clif_displaymessage(fd, msg_txt(262));

	return 0;
}

ACMD_FUNC(unloadnpc)
{
	struct npc_data *nd;
	char NPCname[NAME_LENGTH+1];
	nullpo_retr(-1, sd);

	memset(NPCname, '\0', sizeof(NPCname));

	if (!message || !*message || sscanf(message, "%24[^\n]", NPCname) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um NPC. (uso: @npcoff <nome_NPC>).");
		return -1;
	}

	if ((nd = npc_name2id(NPCname)) == NULL) {
		clif_displaymessage(fd, msg_txt(111)); //Este NPC não existe.
		return -1;
	}

	npc_unload_duplicates(nd);
	npc_unload(nd);
	npc_read_event_script();
	clif_displaymessage(fd, msg_txt(112)); // NPC desativado.
	return 0;
}

/*==========================================
 * tempo em txt atravéz de comando (por [Yor])
 *------------------------------------------*/
char* txt_time(unsigned int duration)
{
	int days, hours, minutes, seconds;
	char temp[CHAT_SIZE_MAX];
	static char temp1[CHAT_SIZE_MAX];

	memset(temp, '\0', sizeof(temp));
	memset(temp1, '\0', sizeof(temp1));

	days = duration / (60 * 60 * 24);
	duration = duration - (60 * 60 * 24 * days);
	hours = duration / (60 * 60);
	duration = duration - (60 * 60 * hours);
	minutes = duration / 60;
	seconds = duration - (60 * minutes);

	if (days < 2)
		sprintf(temp, msg_txt(219), days); // %d dia.
	else
		sprintf(temp, msg_txt(220), days); // %d dias.
	if (hours < 2)
		sprintf(temp1, msg_txt(221), temp, hours); // %s %d hora
	else
		sprintf(temp1, msg_txt(222), temp, hours); // %s %d horas
	if (minutes < 2)
		sprintf(temp, msg_txt(223), temp1, minutes); // %s %d minuto
	else
		sprintf(temp, msg_txt(224), temp1, minutes); // %s %d minutos
	if (seconds < 2)
		sprintf(temp1, msg_txt(225), temp, seconds); // %s e %d segundo
	else
		sprintf(temp1, msg_txt(226), temp, seconds); // %s e %d segundos

	return temp1;
}

/*==========================================
 * @time/@date/@serverdate/@servertime: Mostra a data/horário do server (por [Yor]
 *------------------------------------------*/
ACMD_FUNC(servertime)
{
	const struct TimerData * timer_data;
	const struct TimerData * timer_data2;
	time_t time_server;  // variável para número de segundos. (usado com a função time())
	struct tm *datetime; // variável para tempo na struct ->tm_mday, ->tm_sec, ...
	char temp[CHAT_SIZE_MAX];
	nullpo_retr(-1, sd);

	memset(temp, '\0', sizeof(temp));

	time(&time_server);  // pega o tempo em segundos desde 1/1/1970
	datetime = localtime(&time_server); // converte segundos na estrutura.
	// como sprintf, mas apenas para tempo/data. (Sunday, November 02 2003 15:12:52)
	strftime(temp, sizeof(temp)-1, msg_txt(230), datetime); // Tempo do servidor (tempo normal): %A, %B %d %Y %X.
	clif_displaymessage(fd, temp);

	if (battle_config.night_duration == 0 && battle_config.day_duration == 0) {
		if (night_flag == 0)
			clif_displaymessage(fd, msg_txt(231)); // Tempo no Jogo: O jogo está em dia permanente.
		else
			clif_displaymessage(fd, msg_txt(232)); // Tempo no Jogo: O jogo está em noite permanente.
	} else if (battle_config.night_duration == 0)
		if (night_flag == 1) { // começamos com a noite
			timer_data = get_timer(day_timer_tid);
			sprintf(temp, msg_txt(233), txt_time(DIFF_TICK(timer_data->tick,gettick())/1000)); // Tempo no Jogo: O jogo está de noite por %s.
			clif_displaymessage(fd, temp);
			clif_displaymessage(fd, msg_txt(234)); // Tempo no Jogo: Depois, o jogo estará em dia permanente.
		} else
			clif_displaymessage(fd, msg_txt(231)); // Tempo no Jogo: O jogo está em dia permanente.
	else if (battle_config.day_duration == 0)
		if (night_flag == 0) { // começamos com a noite
			timer_data = get_timer(night_timer_tid);
			sprintf(temp, msg_txt(235), txt_time(DIFF_TICK(timer_data->tick,gettick())/1000)); // Tempo no Jogo: O jogo atualmente está de dia por %s.
			clif_displaymessage(fd, temp);
			clif_displaymessage(fd, msg_txt(236)); //Tempo no Jogo: Depois, o jogo estará em noite permanente.
		} else
			clif_displaymessage(fd, msg_txt(232)); // Tempo no Jogo: O jogo está em noite permanente.
	else {
		if (night_flag == 0) {
			timer_data = get_timer(night_timer_tid);
			timer_data2 = get_timer(day_timer_tid);
			sprintf(temp, msg_txt(235), txt_time(DIFF_TICK(timer_data->tick,gettick())/1000)); // Tempo no Jogo: O jogo atualmente está de dia por %s.
			clif_displaymessage(fd, temp);
			if (DIFF_TICK(timer_data->tick, timer_data2->tick) > 0)
				sprintf(temp, msg_txt(237), txt_time(DIFF_TICK(timer_data->interval,DIFF_TICK(timer_data->tick,timer_data2->tick)) / 1000)); // Game time: After, the game will be in night for %s.
			else
				sprintf(temp, msg_txt(237), txt_time(DIFF_TICK(timer_data2->tick,timer_data->tick)/1000)); // Tempo no Jogo: Depois, o jogo estará de noite por %s.
			clif_displaymessage(fd, temp);
			sprintf(temp, msg_txt(238), txt_time(timer_data->interval / 1000)); // Tempo no Jogo: O ciclo do dia tem um tempo de %s.
			clif_displaymessage(fd, temp);
		} else {
			timer_data = get_timer(day_timer_tid);
			timer_data2 = get_timer(night_timer_tid);
			sprintf(temp, msg_txt(233), txt_time(DIFF_TICK(timer_data->tick,gettick()) / 1000)); // Tempo no Jogo: O jogo está de noite por %s.
			clif_displaymessage(fd, temp);
			if (DIFF_TICK(timer_data->tick,timer_data2->tick) > 0)
				sprintf(temp, msg_txt(239), txt_time((timer_data->interval - DIFF_TICK(timer_data->tick, timer_data2->tick)) / 1000)); // Tempo no Jogo: Depois, o jogo estará de dia por %s.
			else
				sprintf(temp, msg_txt(239), txt_time(DIFF_TICK(timer_data2->tick, timer_data->tick) / 1000)); // Tempo no Jogo: Depois, o jogo estará de dia por %s.
			clif_displaymessage(fd, temp);
			sprintf(temp, msg_txt(238), txt_time(timer_data->interval / 1000)); //Tempo no Jogo: O ciclo do dia tem um tempo de %s.
			clif_displaymessage(fd, temp);
		}
	}

	return 0;
}

//Adicionado por Coltaro
//Nós estamos usando esta função aqui em vez de usar time_t porque ele conta apenas o tempo de prisão do jogador quando ele está online(e desde que a ideia é reduzir a quantidade de minutos um por um em status_change_timer...).
//Bom, usar time_t pode até funcionar mas por alguma razão o código parece maior x_x
static void get_jail_time(int jailtime, int* year, int* month, int* day, int* hour, int* minute)
{
	const int factor_year = 518400; //12*30*24*60 = 518400
	const int factor_month = 43200; //30*24*60 = 43200
	const int factor_day = 1440; //24*60 = 1440
	const int factor_hour = 60;

	*year = jailtime/factor_year;
	jailtime -= *year*factor_year;
	*month = jailtime/factor_month;
	jailtime -= *month*factor_month;
	*day = jailtime/factor_day;
	jailtime -= *day*factor_day;
	*hour = jailtime/factor_hour;
	jailtime -= *hour*factor_hour;
	*minute = jailtime;

	*year = *year > 0? *year : 0;
	*month = *month > 0? *month : 0;
	*day = *day > 0? *day : 0;
	*hour = *hour > 0? *hour : 0;
	*minute = *minute > 0? *minute : 0;
	return;
}

/*==========================================
 * @jail <nome_char> por [Yor]
 * Teletransporte especial! Não checa as mapflags nowarp e nowarpto.
 *------------------------------------------*/
ACMD_FUNC(jail)
{
	struct map_session_data *pl_sd;
	int x, y;
	unsigned short m_index;
	nullpo_retr(-1, sd);

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um jogador (uso: @jail <nome_jogador>).");
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); //Personagem não encontrado.
		return -1;
	}

	if (pc_isGM(sd) < pc_isGM(pl_sd))
  	{ // you can jail only lower or same GM
		clif_displaymessage(fd, msg_txt(81)); // Seu nível de GM não te autoriza a fazer esta ação neste jogador.
		return -1;
	}

	if (pl_sd->sc.data[SC_JAILED])
	{
		clif_displaymessage(fd, msg_txt(118)); // Jogador enviado para a prisão.
		return -1;
	}

	switch(rand() % 2) { //Localizações da prisão
	case 0:
		m_index = mapindex_name2id(MAP_JAIL);
		x = 24;
		y = 75;
		break;
	default:
		m_index = mapindex_name2id(MAP_JAIL);
		x = 49;
		y = 75;
		break;
	}

	//Duração de INT_MAX para um infinitivo específico.
	sc_start4(&pl_sd->bl,SC_JAILED,100,INT_MAX,m_index,x,y,1000);
	clif_displaymessage(pl_sd->fd, msg_txt(117)); //Um GM te enviou para a prisão.
	clif_displaymessage(fd, msg_txt(118)); //Jogador enviado para a prisão.
	return 0;
}

/*==========================================
 * @unjail/@discharge <nome_personagem> por [Yor]
 * Teletransporte especial! Não checa as mapflags nowarp e nowarpto.
 *------------------------------------------*/
ACMD_FUNC(unjail)
{
	struct map_session_data *pl_sd;

	memset(atcmd_player_name, '\0', sizeof(atcmd_player_name));

	if (!message || !*message || sscanf(message, "%23[^\n]", atcmd_player_name) < 1) {
 		clif_displaymessage(fd, "Por favor, entre com o nome de um jogador. (uso: @unjail/@discharge <nome_jogador>).");
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}

	if (pc_isGM(sd) < pc_isGM(pl_sd)) { // Você só pode prender jogadores com o mesmo nível de GM ou abaixo.

		clif_displaymessage(fd, msg_txt(81)); // Seu nível de GM não te autoriza a fazer esta ação neste jogador.
		return -1;
	}

	if (!pl_sd->sc.data[SC_JAILED])
	{
		clif_displaymessage(fd, msg_txt(119)); // Este jogador não está na prisão.
		return -1;
	}

	//Reset jail time to 1 sec.
	sc_start(&pl_sd->bl,SC_JAILED,100,1,1000);
	clif_displaymessage(pl_sd->fd, msg_txt(120)); // Um GM te retirou da prisão.
	clif_displaymessage(fd, msg_txt(121)); // Jogador teletransportado para onde estava.
	return 0;
}

ACMD_FUNC(jailfor)
{
	struct map_session_data *pl_sd = NULL;
	int year, month, day, hour, minute, value;
	char * modif_p;
	int jailtime = 0,x,y;
	short m_index = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%s %23[^\n]",atcmd_output,atcmd_player_name) < 2) {
		clif_displaymessage(fd, msg_txt(400));	//uso: @jailfor <tempo> <nome_personagem>
		return -1;
	}

	atcmd_output[sizeof(atcmd_output)-1] = '\0';

	modif_p = atcmd_output;
	year = month = day = hour = minute = 0;
	while (modif_p[0] != '\0') {
		value = atoi(modif_p);
		if (value == 0)
			modif_p++;
		else {
			if (modif_p[0] == '-' || modif_p[0] == '+')
				modif_p++;
			while (modif_p[0] >= '0' && modif_p[0] <= '9')
				modif_p++;
			if (modif_p[0] == 'n') {
				minute = value;
				modif_p++;
			} else if (modif_p[0] == 'm' && modif_p[1] == 'n') {
				minute = value;
				modif_p = modif_p + 2;
			} else if (modif_p[0] == 'h') {
				hour = value;
				modif_p++;
			} else if (modif_p[0] == 'd' || modif_p[0] == 'j') {
				day = value;
				modif_p++;
			} else if (modif_p[0] == 'm') {
				month = value;
				modif_p++;
			} else if (modif_p[0] == 'y' || modif_p[0] == 'a') {
				year = value;
				modif_p++;
			} else if (modif_p[0] != '\0') {
				modif_p++;
			}
		}
	}

	if (year == 0 && month == 0 && day == 0 && hour == 0 && minute == 0) {
		clif_displaymessage(fd, "Tempo inválido para o comando jail.");
		return -1;
	}

	if ((pl_sd = map_nick2sd(atcmd_player_name)) == NULL) {
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}

	if (pc_isGM(pl_sd) > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(81)); // Seu nível de GM não te autoriza a fazer esta ação neste jogador.
		return -1;
	}

	jailtime = year*12*30*24*60 + month*30*24*60 + day*24*60 + hour*60 + minute;	//Em minutos.

	if(jailtime==0) {
		clif_displaymessage(fd, "Tempo inválido para o comando jail.");
		return -1;
	}

	//Adicionado por Coltaro
	if(pl_sd->sc.data[SC_JAILED] && 
		pl_sd->sc.data[SC_JAILED]->val1 != INT_MAX)
  	{	//Atualiza o tempo de prisão do jogador
		jailtime += pl_sd->sc.data[SC_JAILED]->val1;
		if (jailtime <= 0) {
			jailtime = 0;
			clif_displaymessage(pl_sd->fd, msg_txt(120)); // Um GM te retirou da prisão.
			clif_displaymessage(fd, msg_txt(121)); // Jogador solto.
		} else {
			get_jail_time(jailtime,&year,&month,&day,&hour,&minute);
			sprintf(atcmd_output,msg_txt(402),"Você está",year,month,day,hour,minute); //%s na cadeia por %d anos, %d meses, %d dias, %d horas e %d minutos.
	 		clif_displaymessage(pl_sd->fd, atcmd_output);
			sprintf(atcmd_output,msg_txt(402),"Este jogador está",year,month,day,hour,minute); //%s na cadeia por %d anos, %d meses, %d dias, %d horas e %d minutos.
	 		clif_displaymessage(fd, atcmd_output);
		}
	} else if (jailtime < 0) {
		clif_displaymessage(fd, "Tempo inválido para o comando jail.");
		return -1;
	}

	//Localização da prisão, adicione mais se quiser
	switch(rand()%2)
	{
		case 1: //Prisão #1
			m_index = mapindex_name2id(MAP_JAIL);
			x = 49; y = 75;
			break;
		default: //Prisão padrão.
			m_index = mapindex_name2id(MAP_JAIL);
			x = 24; y = 75;
			break;
	}

	sc_start4(&pl_sd->bl,SC_JAILED,100,jailtime,m_index,x,y,jailtime?60000:1000); //jailtime = 0: O tempo foi resetado para 0. Espera 1 segundo para teletransportar o jogador de volta. 
	return 0;
}


//por Coltaro
ACMD_FUNC(jailtime)
{
	int year, month, day, hour, minute;

	nullpo_retr(-1, sd);
	
	if (!sd->sc.data[SC_JAILED]) {
		clif_displaymessage(fd, "Você não está na prisão.");
		return -1;
	}

	if (sd->sc.data[SC_JAILED]->val1 == INT_MAX) {
		clif_displaymessage(fd, "Você foi preso por tempo indefinido.");
		return 0;
	}

	if (sd->sc.data[SC_JAILED]->val1 <= 0) { // Não foi preso com @jailfor (talvez @jail? ou teleportado ali? ou recebeu recall?)
		clif_displaymessage(fd, "Ninguém sabe por quanto tempo você ficará aqui.");
		return -1;
	}

	//Pega o tempo restante.
	get_jail_time(sd->sc.data[SC_JAILED]->val1,&year,&month,&day,&hour,&minute);
	sprintf(atcmd_output,msg_txt(402),"Você vai continuar",year,month,day,hour,minute); // %s na cadeia por %d anos, %d meses, %d dias, %d horas e %d minutos.

	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * @disguise <mob_id> por [Valaris] (simplificado por [Yor])
 *------------------------------------------*/
ACMD_FUNC(disguise)
{
	int id = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome/id de um Monstro/NPC (uso: @disguise <nome/id>).");
		return -1;
	}

	if ((id = atoi(message)) > 0)
	{
		if (!mobdb_checkid(id) && !npcdb_checkid(id))
			id = 0; //ID inválido.
	}	else	{ 
		if ((id = mobdb_searchname(message)) == 0)
		{
			struct npc_data* nd = npc_name2id(message);
			if (nd != NULL)
				id = nd->class_;
		}
	}

	if (id == 0)
	{
		clif_displaymessage(fd, msg_txt(123));	// Nome/ID de Monstro/NPC não encontrado
		return -1;
	}

	if( pc_isriding(sd, OPTION_RIDING|OPTION_RIDING_DRAGON|OPTION_RIDING_WUG|OPTION_MADO) )
	{
		//FIXME: wrong message [ultramage]
		//clif_displaymessage(fd, msg_txt(227)); // Character cannot wear disguise while riding a PecoPeco.
		return -1;
	}

	pc_disguise(sd, id);
	clif_displaymessage(fd, msg_txt(122)); // Transformação aplicada..

	return 0;
}

/*==========================================
 * DisguiseAll
 *------------------------------------------*/
ACMD_FUNC(disguiseall)
{
	int mob_id=0;
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome/id de um Monstro/NPC (uso: @disguiseall <nome/id>).");
		return -1;
	}

	if ((mob_id = mobdb_searchname(message)) == 0) // checa o nome primeiro(para evitar um possível nome começado com números.)
		mob_id = atoi(message);

	if (!mobdb_checkid(mob_id) && !npcdb_checkid(mob_id)) { //se é um mob ou npc
		clif_displaymessage(fd, msg_txt(123)); // Nome/ID de Monstro/NPC não encontrado.
		return -1;
	}

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		pc_disguise(pl_sd, mob_id);
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(122)); // Transformação aplicada.
	return 0;
}

/*==========================================
 * @undisguise by [Yor]
 *------------------------------------------*/
ACMD_FUNC(undisguise)
{
	nullpo_retr(-1, sd);
	if (sd->disguise) {
		pc_disguise(sd, 0);
		clif_displaymessage(fd, msg_txt(124)); // Destransformação aplicada.
	} else {
		clif_displaymessage(fd, msg_txt(125)); // Você não está transformado.
		return -1;
	}

	return 0;
}

/*==========================================
 * UndisguiseAll
 *------------------------------------------*/
ACMD_FUNC(undisguiseall)
{
	struct map_session_data *pl_sd;
	struct s_mapiterator* iter;
	nullpo_retr(-1, sd);

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
		if( pl_sd->disguise )
			pc_disguise(pl_sd, 0);
	mapit_free(iter);

	clif_displaymessage(fd, msg_txt(124)); // Destransformação aplicada.

	return 0;
}

/*==========================================
 * @exp por [Skotlex]
 *------------------------------------------*/
ACMD_FUNC(exp)
{
	char output[CHAT_SIZE_MAX];
	double nextb, nextj;
	nullpo_retr(-1, sd);
	memset(output, '\0', sizeof(output));
	
	nextb = pc_nextbaseexp(sd);
	if (nextb)
		nextb = sd->status.base_exp*100.0/nextb;
	
	nextj = pc_nextjobexp(sd);
	if (nextj)
		nextj = sd->status.job_exp*100.0/nextj;
	
	sprintf(output, "Nível de base: %d (%.3f%%) | Nível de classe: %d (%.3f%%)", sd->status.base_level, nextb, sd->status.job_level, nextj);
	clif_displaymessage(fd, output);
	return 0;
}


/*==========================================
 * @broadcast by [Valaris]
 *------------------------------------------*/
ACMD_FUNC(broadcast)
{
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com uma mensagem (uso: @broadcast <mensagem>).");
		return -1;
	}

	sprintf(atcmd_output, "%s: %s", sd->status.name, message);
	intif_broadcast(atcmd_output, strlen(atcmd_output) + 1, 0);

	return 0;
}

/*==========================================
 * @localbroadcast por [Valaris]
 *------------------------------------------*/
ACMD_FUNC(localbroadcast)
{
	nullpo_retr(-1, sd);

	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com uma mensagem (uso: @localbroadcast <mensagem>).");
		return -1;
	}

	sprintf(atcmd_output, "%s: %s", sd->status.name, message);

	clif_broadcast(&sd->bl, atcmd_output, strlen(atcmd_output) + 1, 0, ALL_SAMEMAP);

	return 0;
}

/*==========================================
 * @email <atual@email> <novo@email> por [Yor]
 *------------------------------------------*/
ACMD_FUNC(email)
{
	char actual_email[100];
	char new_email[100];
	nullpo_retr(-1, sd);

	memset(actual_email, '\0', sizeof(actual_email));
	memset(new_email, '\0', sizeof(new_email));

	if (!message || !*message || sscanf(message, "%99s %99s", actual_email, new_email) < 2) {
		clif_displaymessage(fd, "Por favor entre 2 e-mails (uso: @email <atual@email> <novo@email>).");
		return -1;
	}

	if (e_mail_check(actual_email) == 0) {
		clif_displaymessage(fd, msg_txt(144)); // E-mail atual inválido. Se você usa o email padrão, digite a@a.com.
		return -1;
	} else if (e_mail_check(new_email) == 0) {
		clif_displaymessage(fd, msg_txt(145)); // Novo e-mail Inválido. Por favor entre com um email real.
		return -1;
	} else if (strcmpi(new_email, "a@a.com") == 0) {
		clif_displaymessage(fd, msg_txt(146)); // Novo e-mail já deve ser um e-mail existente..
		return -1;
	} else if (strcmpi(actual_email, new_email) == 0) {
		clif_displaymessage(fd, msg_txt(147)); // Novo email deve ser diferente do email atual.
		return -1;
	}

	chrif_changeemail(sd->status.account_id, actual_email, new_email);
	clif_displaymessage(fd, msg_txt(148)); // Informações enviadas ao Servidor de login (login-server) via Servidor de personagens (char-server).
	return 0;
}

/*==========================================
 *@effect
 *------------------------------------------*/
ACMD_FUNC(effect)
{
	int type = 0, flag = 0;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d", &type) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o número de um efeito (uso: @effect <número_efeito>).");
		return -1;
	}

	clif_specialeffect(&sd->bl, type, (send_target)flag);
	clif_displaymessage(fd, msg_txt(229)); // Efeito alterado.
	return 0;
}

/*==========================================
 * @killer por MouseJstr
 * permite matar jogadores mesmo sem pvp
 *------------------------------------------*/
ACMD_FUNC(killer)
{
	nullpo_retr(-1, sd);
	sd->state.killer = !sd->state.killer;

	if(sd->state.killer)
		clif_displaymessage(fd, msg_txt(241)); //Agora você pode matar qualquer um.
	else {
		clif_displaymessage(fd, msg_txt(287)); // O status 'Personagem pode ser morto' foi resetado.
		pc_stop_attack(sd);
	}
	return 0;
}

/*==========================================
 * @killable por MouseJstr
 * permite que outras pessoas matem você
 *------------------------------------------*/
ACMD_FUNC(killable)
{
	nullpo_retr(-1, sd);
	sd->state.killable = !sd->state.killable;

	if(sd->state.killable)
		clif_displaymessage(fd, msg_txt(242));
	else {
		clif_displaymessage(fd, msg_txt(288));
		map_foreachinrange(atcommand_stopattack,&sd->bl, AREA_SIZE, BL_CHAR, sd->bl.id);
	}
	return 0;
}

/*==========================================
 * @skillon por MouseJstr
 * permite o uso de skills no mapa
 *------------------------------------------*/
ACMD_FUNC(skillon)
{
	nullpo_retr(-1, sd);
	map[sd->bl.m].flag.noskill = 0;
	clif_displaymessage(fd, msg_txt(244));
	return 0;
}

/*==========================================
 * @skilloff por MouseJstr
 * Não permite o uso de skills no mapa
 *------------------------------------------*/
ACMD_FUNC(skilloff)
{
	nullpo_retr(-1, sd);
	map[sd->bl.m].flag.noskill = 1;
	clif_displaymessage(fd, msg_txt(243));
	return 0;
}

/*==========================================
 * @npcmove por MouseJstr
 * Move um NPC
 *------------------------------------------*/
ACMD_FUNC(npcmove)
{
	int x = 0, y = 0, m;
	struct npc_data *nd = 0;
	nullpo_retr(-1, sd);
	memset(atcmd_player_name, '\0', sizeof atcmd_player_name);

	if (!message || !*message || sscanf(message, "%d %d %23[^\n]", &x, &y, atcmd_player_name) < 3) {
		clif_displaymessage(fd, "uso: @npcmove <X> <Y> <nome_npc>");
		return -1;
	}

	if ((nd = npc_name2id(atcmd_player_name)) == NULL)
	{
		clif_displaymessage(fd, msg_txt(111)); // This NPC doesn't exist.
		return -1;
	}

	if ((m=nd->bl.m) < 0 || nd->bl.prev == NULL)
	{
		clif_displaymessage(fd, "O NPC não está neste mapa");
		return -1;
	}
	
	x = cap_value(x, 0, map[m].xs-1);
	y = cap_value(y, 0, map[m].ys-1);
	map_foreachinrange(clif_outsight, &nd->bl, AREA_SIZE, BL_PC, &nd->bl);
	map_moveblock(&nd->bl, x, y, gettick());
	map_foreachinrange(clif_insight, &nd->bl, AREA_SIZE, BL_PC, &nd->bl);
	clif_displaymessage(fd, "NPC movido.");

	return 0;
}

/*==========================================
 * @addwarp por MouseJstr
 * Cria um novo ponto de teletransporte estático.
 *------------------------------------------*/
ACMD_FUNC(addwarp)
{
	char mapname[32];
	int x,y;
	unsigned short m;
	struct npc_data* nd;

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%31s %d %d", mapname, &x, &y) < 3) {
		clif_displaymessage(fd, "uso: @addwarp <nome_mapa> <X> <Y>.");
		return -1;
	}

	m = mapindex_name2id(mapname);
	if( m == 0 )
	{
		sprintf(atcmd_output, "Mapa '%s' desconhecido.", mapname);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}

	nd = npc_add_warp(sd->bl.m, sd->bl.x, sd->bl.y, 2, 2, m, x, y);
	if( nd == NULL )
		return -1;

	sprintf(atcmd_output, "O novo NPC '%s' foi criado.", nd->exname);
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * @follow por [MouseJstr]
 * Segue um jogador .. ficando mais que 5 células de distância
 *------------------------------------------*/
ACMD_FUNC(follow)
{
	struct map_session_data *pl_sd = NULL;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		if (sd->followtarget == -1)
			return -1;

		pc_stop_following (sd);
		clif_displaymessage(fd, "Modo seguir desligado.");
		return 0;
	}
	
	if ( (pl_sd = map_nick2sd((char *)message)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}

	if (sd->followtarget == pl_sd->bl.id) {
		pc_stop_following (sd);
		clif_displaymessage(fd, "Modo seguir desligado.");
	} else {
		pc_follow(sd, pl_sd->bl.id);
		clif_displaymessage(fd, "Modo seguir ligado.");
	}
	
	return 0;
}


/*==========================================
 * @dropall por [MouseJstr]
 * Derruba tudo o que você tiver no chão.
 *------------------------------------------*/
ACMD_FUNC(dropall)
{
	int i;
	nullpo_retr(-1, sd);
	for (i = 0; i < MAX_INVENTORY; i++) {
	if (sd->status.inventory[i].amount) {
		if(sd->status.inventory[i].equip != 0)
			pc_unequipitem(sd, i, 3);
			pc_dropitem(sd,  i, sd->status.inventory[i].amount);
		}
	}
	return 0;
}

/*==========================================
 * @storeall por [MouseJstr]
 * Poe tudo no armazém.
 *------------------------------------------*/
ACMD_FUNC(storeall)
{
	int i;
	nullpo_retr(-1, sd);

	if (sd->state.storage_flag != 1)
  	{	//Abre o armazém.
		if( storage_storageopen(sd) == 1 ) {
			clif_displaymessage(fd, "Você não pode abrir o armazém agora.");
			return -1;
		}
	}

	for (i = 0; i < MAX_INVENTORY; i++) {
		if (sd->status.inventory[i].amount) {
			if(sd->status.inventory[i].equip != 0)
				pc_unequipitem(sd, i, 3);
			storage_storageadd(sd,  i, sd->status.inventory[i].amount);
		}
	}
	storage_storageclose(sd);

	clif_displaymessage(fd, "Está feito.");
	return 0;
}

/*==========================================
 * @skillid by [MouseJstr]
 * lookup a skill by name
 *------------------------------------------*/
ACMD_FUNC(skillid)
{
	int skillen, idx;
	nullpo_retr(-1, sd);

	if (!message || !*message)
	{
		clif_displaymessage(fd, "Por favor entre com o nome de uma habilidade para avaliar (uso: @skillid <nome_habilidade>).");
		return -1;
	}

	skillen = strlen(message);

	for (idx = 0; idx < MAX_SKILL_DB; idx++) {
		if (strnicmp(skill_db[idx].name, message, skillen) == 0 || strnicmp(skill_db[idx].desc, message, skillen) == 0)
		{
			sprintf(atcmd_output, "Habilidade %d: %s", idx, skill_db[idx].desc);
			clif_displaymessage(fd, atcmd_output);
		}
	}

	return 0;
}

/*==========================================
 * @useskill por [MouseJstr]
 * Uma forma de usar skills sem ter que achar elas no menu de skills.
 *------------------------------------------*/
ACMD_FUNC(useskill)
{
	struct map_session_data *pl_sd = NULL;
	struct block_list *bl;
	int skillnum;
	int skilllv;
	char target[100];
	nullpo_retr(-1, sd);

	if(!message || !*message || sscanf(message, "%d %d %23[^\n]", &skillnum, &skilllv, target) != 3) {
		clif_displaymessage(fd, "uso: @useskill <número_habilidade> <nível_habilidade> <alvo>");
		return -1;
	}

	if ( (pl_sd = map_nick2sd(target)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}

	if ( pc_isGM(sd) < pc_isGM(pl_sd) )
	{
		clif_displaymessage(fd, msg_txt(81)); // Seu nível de GM não te autoriza a fazer esta ação neste jogador.
		return -1;
	}

	if (skillnum >= HM_SKILLBASE && skillnum < HM_SKILLBASE+MAX_HOMUNSKILL
		&& sd->hd && merc_is_hom_active(sd->hd)) 
		bl = &sd->hd->bl;
	else
		bl = &sd->bl;
	
	if (skill_get_inf(skillnum)&INF_GROUND_SKILL)
		unit_skilluse_pos(bl, pl_sd->bl.x, pl_sd->bl.y, skillnum, skilllv);
	else
		unit_skilluse_id(bl, pl_sd->bl.id, skillnum, skilllv);

	return 0;
}

/*==========================================
 * @displayskill por [Skotlex]
 *  Debuga o comando para localizar um novo ID para a habilidade. Manda
 *  possíveis pacotes de efeito de skill para a área.
 *------------------------------------------*/
ACMD_FUNC(displayskill)
{
	struct status_data * status;
	unsigned int tick;
	int skillnum;
	int skilllv = 1;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %d", &skillnum, &skilllv) < 1)
	{
		clif_displaymessage(fd, "uso: @displayskill <número_habilidade> {<nível_habilidade>}>");
		return -1;
	}
	status = status_get_status_data(&sd->bl);
	tick = gettick();
	clif_skill_damage(&sd->bl,&sd->bl, tick, status->amotion, status->dmotion, 1, 1, skillnum, skilllv, 5);
	clif_skill_nodamage(&sd->bl, &sd->bl, skillnum, skilllv, 1);
	clif_skill_poseffect(&sd->bl, skillnum, skilllv, sd->bl.x, sd->bl.y, tick);
	return 0;
}

/*==========================================
 * @skilltree por [MouseJstr]
 * Mostra toda a árvore de habilidades para um jogador.
 *------------------------------------------*/
ACMD_FUNC(skilltree)
{
	struct map_session_data *pl_sd = NULL;
	int skillnum;
	int meets, j, c=0;
	char target[NAME_LENGTH];
	struct skill_tree_entry *ent;
	nullpo_retr(-1, sd);

	if(!message || !*message || sscanf(message, "%d %23[^\r\n]", &skillnum, target) != 2) {
		clif_displaymessage(fd, "uso: @skilltree <número_habilidade> <alvo>");
		return -1;
	}

	if ( (pl_sd = map_nick2sd(target)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}

	c = pc_calc_skilltree_normalize_job(pl_sd);
	c = pc_mapid2jobid(c, pl_sd->status.sex);

	sprintf(atcmd_output, "O jogador está usando a árvore de habilidades de um %s (%d pontos básicos)", job_name(c), pc_checkskill(pl_sd, 1));
	clif_displaymessage(fd, atcmd_output);

	ARR_FIND( 0, MAX_SKILL_TREE, j, skill_tree[c][j].id == 0 || skill_tree[c][j].id == skillnum );
	if( j == MAX_SKILL_TREE || skill_tree[c][j].id == 0 )
	{
		sprintf(atcmd_output, "Não acredito que o jogador pode usar esta habilidade !");
		clif_displaymessage(fd, atcmd_output);
		return 0;
	}

	ent = &skill_tree[c][j];

	meets = 1;
	for(j=0;j<MAX_PC_SKILL_REQUIRE;j++)
	{
		if( ent->need[j].id && pc_checkskill(sd,ent->need[j].id) < ent->need[j].lv)
		{
			sprintf(atcmd_output, "O jogador precisa de %d níveis para a habilidade %s", ent->need[j].lv, skill_db[ent->need[j].id].desc);
			clif_displaymessage(fd, atcmd_output);
			meets = 0;
		}
	}
	if (meets == 1) {
		sprintf(atcmd_output, "Acredito que o jogador tem todos os requerimentos para esta habilidade.");
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

// Coloca um anel com o nome de seu parceiro(a)
void getring (struct map_session_data* sd)
{
	int flag, item_id;
	struct item item_tmp;
	item_id = (sd->status.sex) ? WEDDING_RING_M : WEDDING_RING_F;

	memset(&item_tmp, 0, sizeof(item_tmp));
	item_tmp.nameid = item_id;
	item_tmp.identify = 1;
	item_tmp.card[0] = 255;
	item_tmp.card[2] = sd->status.partner_id;
	item_tmp.card[3] = sd->status.partner_id >> 16;

	//Logs (A)dmins items [Lupus]
	if(log_config.enable_logs&0x400)
		log_pick_pc(sd, "A", item_id, 1, &item_tmp);

	if((flag = pc_additem(sd,&item_tmp,1))) {
		clif_additem(sd,0,0,flag);
		map_addflooritem(&item_tmp,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0);
	}
}

/*==========================================
 * @marry pot [MouseJstr], concertado por Lupus
 * Casa dois jogadores.
 *------------------------------------------*/
ACMD_FUNC(marry)
{
  struct map_session_data *pl_sd1 = NULL;
  struct map_session_data *pl_sd2 = NULL;
  char player1[128], player2[128]; //Tamanho usado para retornar erros.

  nullpo_retr(-1, sd);

  if (!message || !*message || sscanf(message, "%23[^,], %23[^\r\n]", player1, player2) != 2) {
	clif_displaymessage(fd, "uso: @marry <jogador1>,<jogador2>");
	return -1;
  }

  if((pl_sd1=map_nick2sd((char *) player1)) == NULL) {
	sprintf(player2, "Não se pode achar o jogador '%s' online.", player1);
	clif_displaymessage(fd, player2);
	return -1;
  }

  if((pl_sd2=map_nick2sd((char *) player2)) == NULL) {
	sprintf(player1, "Não se pode achar o jogador '%s' online.", player2);
	clif_displaymessage(fd, player1);
	return -1;
  }

  if (pc_marriage(pl_sd1, pl_sd2) == 0) {
	clif_displaymessage(fd, "Eles estão casados agora. Deseje-os bem !");
	clif_wedding_effect(&sd->bl);	//efeito e música de casamento [Lupus]
	// Da automaticamente os aneis. (Aru)
	getring (pl_sd1);
	getring (pl_sd2);
	return 0;
  }

	clif_displaymessage(fd, "Ambos não podem casar pois um deles é um bebê ou já é casado.");
	return -1;
}

/*==========================================
 * @divorce por [MouseJstr], concertado por [Lupus]
 * divorcia dois jogadores.
 *------------------------------------------*/
ACMD_FUNC(divorce)
{
  struct map_session_data *pl_sd = NULL;

  nullpo_retr(-1, sd);

  if (!message || !*message || sscanf(message, "%23[^\r\n]", atcmd_player_name) != 1) {
	clif_displaymessage(fd, "uso: @divorce <jogador>.");
	return -1;
  }

	if ( (pl_sd = map_nick2sd(atcmd_player_name)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); //Personagem não encontrado.
		return -1;
	}

	if (pc_divorce(pl_sd) != 0) {
		sprintf(atcmd_output, "O divórcio falhou.. Não se pode achar '%s' ou seu(sua) parceiro(a) online.", atcmd_player_name);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}
	
	sprintf(atcmd_output, "'%s' e seu(sua) parceiro(a) estão agora divorciados.", atcmd_player_name);
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * @changelook por [Celest]
 *------------------------------------------*/
ACMD_FUNC(changelook)
{
	int i, j = 0, k = 0;
	int pos[6] = { LOOK_HEAD_TOP,LOOK_HEAD_MID,LOOK_HEAD_BOTTOM,LOOK_WEAPON,LOOK_SHIELD,LOOK_SHOES };

	if((i = sscanf(message, "%d %d", &j, &k)) < 1) {
		clif_displaymessage(fd, "uso: @changelook [<posição>] <id> -- [] = opcional");
		clif_displaymessage(fd, "Posição: 1-Topo 2-Meio 3-Baixo 4-Arma 5-Escudo");
		return -1;
	} else if (i == 2) {
		if (j < 1) j = 1;
		else if (j > 6) j = 6;	// 6 = sapatos - em clientes beta talvez
		j = pos[j - 1];
	} else if (i == 1) {	// posição não definida, use HEAD_TOP como padrão
		k = j;	
		j = LOOK_HEAD_TOP;
	}

	clif_changelook(&sd->bl,j,k);

	return 0;
}

/*==========================================
 * @autotrade por durf [Lupus] [Paradox924X]
 * Liga/Desliga autotrade para um jogador específico.
 *------------------------------------------*/
ACMD_FUNC(autotrade)
{
	nullpo_retr(-1, sd);
	
	if( map[sd->bl.m].flag.autotrade != battle_config.autotrade_mapflag ) {
		clif_displaymessage(fd, "Autotrade não é permitida neste mapa.");
		return -1;
	}

	if( pc_isdead(sd) ) {
		clif_displaymessage(fd, "Você não pode usar autotrade morto");
		return -1;
	}
	
	if( !sd->state.vending && !sd->state.buyingstore ) { //checa se o jogador está comprando/vendendo.
		clif_displaymessage(fd, msg_txt(549)); // "Você deve abrir uma loja antes de usar @autotrade."
		return -1;
	}
	
	sd->state.autotrade = 1;
	if( battle_config.at_timeout )
	{
		int timeout = atoi(message);
		status_change_start(&sd->bl, SC_AUTOTRADE, 10000, 0, 0, 0, 0, ((timeout > 0) ? min(timeout,battle_config.at_timeout) : battle_config.at_timeout) * 60000, 0);
	}
	clif_authfail_fd(fd, 15);
		
	return 0;
}

/*==========================================
 * @changegm por durf (alterado por Lupus)
 * Muda a liderança de um clã.
 *------------------------------------------*/
ACMD_FUNC(changegm)
{
	struct guild *g;
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	if (sd->status.guild_id == 0 || (g = guild_search(sd->status.guild_id)) == NULL || strcmp(g->master,sd->status.name))
	{
		clif_displaymessage(fd, "Você precisa ser líder de um clã para fazer isso.");
		return -1;
	}

	if( map[sd->bl.m].flag.guildlock )
	{
		clif_displaymessage(fd, "Você não pode mudar o líder neste mapa.");
		return -1;
	}

	if( !message[0] )
	{
		clif_displaymessage(fd, "Uso de comando: @changegm <nome_futuro_líder>");
		return -1;
	}
	
	if((pl_sd=map_nick2sd((char *) message)) == NULL || pl_sd->status.guild_id != sd->status.guild_id) {
		clif_displaymessage(fd, "O alvo precisa estar online e precisa estar no mesmo clã.");
		return -1;
	}

	guild_gm_change(sd->status.guild_id, pl_sd);
	return 0;
}

/*==========================================
 * @changeleader por Skotlex
 * Muda o líder de um grupo.
 *------------------------------------------*/
ACMD_FUNC(changeleader)
{
	nullpo_retr(-1, sd);
	
	if( !message[0] )
	{
		clif_displaymessage(fd, "Uso de comando: @changeleader <nome_futuro_líder>");
		return -1;
	}

	if (party_changeleader(sd, map_nick2sd((char *) message)))
		return 0;
	return -1;
}

/*==========================================
 * @partyoption por Skotlex
 * Usado para mudar a configuração de compartilhamento de um grupo.
 *------------------------------------------*/
ACMD_FUNC(partyoption)
{
	struct party_data *p;
	int mi, option;
	char w1[16], w2[16];
	nullpo_retr(-1, sd);

	if (sd->status.party_id == 0 || (p = party_search(sd->status.party_id)) == NULL)
	{
		clif_displaymessage(fd, msg_txt(282)); //Você precisa ser o líder do grupo para usar este comando.
		return -1;
	}

	ARR_FIND( 0, MAX_PARTY, mi, p->data[mi].sd == sd );
	if (mi == MAX_PARTY)
		return -1; //Não deveria acontecer.

	if (!p->party.member[mi].leader)
	{
		clif_displaymessage(fd, msg_txt(282)); //Você precisa ser o líder do grupo para usar este comando.
		return -1;
	}

	if(!message || !*message || sscanf(message, "%15s %15s", w1, w2) < 2)
	{
		clif_displaymessage(fd, "Uso de comando: @partyoption <compartilhamento de coleta: yes/no> <distribuição de item: yes/no>");
		return -1;
	}
	
	option = (config_switch(w1)?1:0)|(config_switch(w2)?2:0);

	//Muda o tipo de compartilhamento.
	if (option != p->party.item)
		party_changeoption(sd, p->party.exp, option);
	else
		clif_displaymessage(fd, msg_txt(286));

	return 0;
}

/*==========================================
 * @autoloot por Upa-Kun
 * Liga/Desliga o autollot no jogador.
 *------------------------------------------*/
ACMD_FUNC(autoloot)
{
	int rate;
	double drate;
	nullpo_retr(-1, sd);
	// comando autoloot sem um valor.
	if(!message || !*message)
	{
		if (sd->state.autoloot)
			rate = 0;
		else
			rate = 10000;
	} else {
		drate = atof(message);
		rate = (int)(drate*100);
	}
	if (rate < 0) rate = 0;
	if (rate > 10000) rate = 10000;

	sd->state.autoloot = rate;
	if (sd->state.autoloot) {
		snprintf(atcmd_output, sizeof atcmd_output, "Autoloot de itens com as taxas de drop em %0.02f%% e abaixo.",((double)sd->state.autoloot)/100.);
		clif_displaymessage(fd, atcmd_output); //
	}else
		clif_displaymessage(fd, "Autoloot está desligado.");

	return 0;
}

/*==========================================
 * @alootid
 *------------------------------------------*/
ACMD_FUNC(autolootitem)
{
	struct item_data *item_data = NULL;

	if (!message || !*message) {
		if (sd->state.autolootid) {
			sd->state.autolootid = 0;
			clif_displaymessage(fd, "Autolootitem foi desligado.");
		} else
			clif_displaymessage(fd, "Por favor, entre com o nome/id de um item (uso: @alootid <nome ou id do item>).");

		return -1;
	}

	if ((item_data = itemdb_exists(atoi(message))) == NULL)
		item_data = itemdb_searchname(message);

	if (!item_data) {
		// Nenhum item encontrado na DB.
		clif_displaymessage(fd, "Item não encontrado.");
		return -1;
	}

	sd->state.autolootid = item_data->nameid; // Autoloot ativado.

	sprintf(atcmd_output, "Autocoletando o item: '%s'/'%s' (%d)",
		item_data->name, item_data->jname, item_data->nameid);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * Isso faz chover
 *------------------------------------------*/
ACMD_FUNC(rain)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.rain) {
		map[sd->bl.m].flag.rain=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "A chuva parou.");
	} else {
		map[sd->bl.m].flag.rain=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Começou a chover.");
	}
	return 0;
}

/*==========================================
 * Isso faz nevar.
 *------------------------------------------*/
ACMD_FUNC(snow)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.snow) {
		map[sd->bl.m].flag.snow=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "A neve parou de cair.");
	} else {
		map[sd->bl.m].flag.snow=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Começou a nevar.");
	}

	return 0;
}

/*==========================================
 * As folhas de cerejeira começam a cair.  (Sakura)
 *------------------------------------------*/
ACMD_FUNC(sakura)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.sakura) {
		map[sd->bl.m].flag.sakura=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "As folhas da cerejeira não caem mais.");
	} else {
		map[sd->bl.m].flag.sakura=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "As folhas de cerejeira começaram a cair.");
	}
	return 0;
}

/*==========================================
 * Clouds appear.
 *------------------------------------------*/
ACMD_FUNC(clouds)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.clouds) {
		map[sd->bl.m].flag.clouds=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "As núvens desapareceram.");
	} else {
		map[sd->bl.m].flag.clouds=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "As núvens apareceram.");
	}

	return 0;
}

/*==========================================
 * Diferentes tipos de núvem usando o efeito 516
 *------------------------------------------*/
ACMD_FUNC(clouds2)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.clouds2) {
		map[sd->bl.m].flag.clouds2=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "As núvens alternativas desapareceram.");
	} else {
		map[sd->bl.m].flag.clouds2=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "As núvens alternativas apareceram.");
	}

	return 0;
}

/*==========================================
 * A neblina cobre o lugar.
 *------------------------------------------*/
ACMD_FUNC(fog)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.fog) {
		map[sd->bl.m].flag.fog=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "A neblina se foi.");
	} else {
		map[sd->bl.m].flag.fog=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "A neblina está pairando.");
	}
		return 0;
}

/*==========================================
 * Folhas soltas caem.
 *------------------------------------------*/
ACMD_FUNC(leaves)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.leaves) {
		map[sd->bl.m].flag.leaves=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "As folhas não caem mais.");
	} else {
		map[sd->bl.m].flag.leaves=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "As folhas soltas começaram a cair.");
	}

	return 0;
}

/*==========================================
 * Fogos de artifício aparecem
 *------------------------------------------*/
ACMD_FUNC(fireworks)
{
	nullpo_retr(-1, sd);
	if (map[sd->bl.m].flag.fireworks) {
		map[sd->bl.m].flag.fireworks=0;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Os fogos de artifício pararam.");
	} else {
		map[sd->bl.m].flag.fireworks=1;
		clif_weather(sd->bl.m);
		clif_displaymessage(fd, "Os fogos de artifício foram lançados.");
	}

	return 0;
}

/*==========================================
 * Efeitos de limpeza do tempo por Dexity
 *------------------------------------------*/
ACMD_FUNC(clearweather)
{
	nullpo_retr(-1, sd);
	map[sd->bl.m].flag.rain=0;
	map[sd->bl.m].flag.snow=0;
	map[sd->bl.m].flag.sakura=0;
	map[sd->bl.m].flag.clouds=0;
	map[sd->bl.m].flag.clouds2=0;
	map[sd->bl.m].flag.fog=0;
	map[sd->bl.m].flag.fireworks=0;
	map[sd->bl.m].flag.leaves=0;
	clif_weather(sd->bl.m);
	clif_displaymessage(fd, msg_txt(291));//Os efeitos do tempo serão atualizados.
	
	return 0;
}

/*===============================================================
 * Comando de som - toca um som para todos em volta! por [Codemaster]
 *---------------------------------------------------------------*/
ACMD_FUNC(sound)
{
	char sound_file[100];

	memset(sound_file, '\0', sizeof(sound_file));

		if(!message || !*message || sscanf(message, "%99[^\n]", sound_file) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um arquivo de som. (uso: @sound <nome_arquivo>)");
		return -1;
	}

	if(strstr(sound_file, ".wav") == NULL)
		strcat(sound_file, ".wav");

	clif_soundeffectall(&sd->bl, sound_file, 0, AREA);

	return 0;
}

/*==========================================
 *  Procura de monstros.
 *------------------------------------------*/
ACMD_FUNC(mobsearch)
{
	char mob_name[100];
	int mob_id;
	int number = 0;
	struct s_mapiterator* it;

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%99[^\n]", mob_name) < 1) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um monstro. (uso: @mobsearch <nome_monstro>).");
		return -1;
	}

	if ((mob_id = atoi(mob_name)) == 0)
		 mob_id = mobdb_searchname(mob_name);
	if(mob_id > 0 && mobdb_checkid(mob_id) == 0){
		snprintf(atcmd_output, sizeof atcmd_output, "ID %s inválido.",mob_name);
		clif_displaymessage(fd, atcmd_output);
		return -1;
	}
	if(mob_id == atoi(mob_name) && mob_db(mob_id)->jname)
				strcpy(mob_name,mob_db(mob_id)->jname);	// --ja--
//				strcpy(mob_name,mob_db(mob_id)->name);	// --en--

	snprintf(atcmd_output, sizeof atcmd_output, "Procurando monstro... %s %s", mob_name, mapindex_id2name(sd->mapindex));
	clif_displaymessage(fd, atcmd_output);

	it = mapit_geteachmob();
	for(;;)
	{
		TBL_MOB* md = (TBL_MOB*)mapit_next(it);
		if( md == NULL )
			break;// sem mais monstros.

		if( md->bl.m != sd->bl.m )
			continue;
		if( mob_id != -1 && md->class_ != mob_id )
			continue;

		++number;
		if( md->spawn_timer == INVALID_TIMER )
			snprintf(atcmd_output, sizeof(atcmd_output), "%2d[%3d:%3d] %s", number, md->bl.x, md->bl.y, md->name);
		else
			snprintf(atcmd_output, sizeof(atcmd_output), "%2d[%s] %s", number, "morto", md->name);
		clif_displaymessage(fd, atcmd_output);
	}
	mapit_free(it);

	return 0;
}

/*==========================================
 * @cleanmap - limpa os itens do chão.
 *------------------------------------------*/
static int atcommand_cleanmap_sub(struct block_list *bl, va_list ap)
{
	nullpo_ret(bl);
	map_clearflooritem(bl->id);

	return 0;
}

ACMD_FUNC(cleanmap)
{
	map_foreachinarea(atcommand_cleanmap_sub, sd->bl.m,
		sd->bl.x-AREA_SIZE*2, sd->bl.y-AREA_SIZE*2,
		sd->bl.x+AREA_SIZE*2, sd->bl.y+AREA_SIZE*2,
		BL_ITEM);
	clif_displaymessage(fd, "Todos os itens do chão foram retirados.");
	return 0;
}

/*==========================================
 * make a NPC/PET talk
 * @npctalkc [SnakeDrak]
 *------------------------------------------*/
ACMD_FUNC(npctalk)
{
	char name[NAME_LENGTH],mes[100],temp[100];
	struct npc_data *nd;
	bool ifcolor=(*(command + 8) != 'c' && *(command + 8) != 'C')?0:1;
	unsigned long color=0;

	if (sd->sc.count && // sem conversa enquanto mudo.
		(sd->sc.data[SC_BERSERK] ||
		(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCHAT) ||
		(sd->sc.data[SC_DEEPSLEEP] && sd->sc.data[SC_DEEPSLEEP]->val2) ||
		sd->sc.data[SC_SATURDAYNIGHTFEVER]))
		return -1;

	if(!ifcolor) {
		if (!message || !*message || sscanf(message, "%23[^,], %99[^\n]", name, mes) < 2) {
			clif_displaymessage(fd, "Por favor, entre com a informação correta. (uso: @npctalk <nome_npc>, <mensagem>).");
			return -1;
		}
	}
	else {
		if (!message || !*message || sscanf(message, "%lx %23[^,], %99[^\n]", &color, name, mes) < 3) {
			clif_displaymessage(fd, "Por favor, entre com a informação correta. (uso: @npctalkc <cor> <nome_npc>, <mensagem>).");
			return -1;
		}
	}

	if (!(nd = npc_name2id(name))) {
		clif_displaymessage(fd, msg_txt(111)); // Este NPC não existe.
		return -1;
	}
	
	strtok(name, "#"); // Descarta identificador de nomes extra se existir.
	snprintf(temp, sizeof(temp), "%s : %s", name, mes);
	
	if(ifcolor) clif_messagecolor(&nd->bl,color,temp);
	else clif_message(&nd->bl, temp);

	return 0;
}

ACMD_FUNC(pettalk)
{
	char mes[100],temp[100];
	struct pet_data *pd;

	nullpo_retr(-1, sd);

	if(!sd->status.pet_id || !(pd=sd->pd))
	{
		clif_displaymessage(fd, msg_txt(184));
		return -1;
	}

	if (sd->sc.count && // sem conversa enquanto mudo.
		(sd->sc.data[SC_BERSERK] ||
		(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCHAT) ||
		(sd->sc.data[SC_DEEPSLEEP] && sd->sc.data[SC_DEEPSLEEP]->val2) ||
		sd->sc.data[SC_SATURDAYNIGHTFEVER]))
		return -1;

	if (!message || !*message || sscanf(message, "%99[^\n]", mes) < 1) {
		clif_displaymessage(fd, "Por favor, entre com uma mensagem. (uso: @pettalk <mensagem>");
		return -1;
	}

	if (message[0] == '/')
	{
		const char* emo[] = {
			"/!", "/?", "/ho", "/lv", "/swt", "/ic", "/an", "/ag", "/$", "/...",
			"/scissors", "/rock", "/paper", "/korea", "/lv2", "/thx", "/wah", "/sry", "/heh", "/swt2",
			"/hmm", "/no1", "/??", "/omg", "/O", "/X", "/hlp", "/go", "/sob", "/gg",
			"/kis", "/kis2", "/pif", "/ok", "-?-", "-?-", "/bzz", "/rice", "/awsm", "/meh",
			"/shy", "/pat", "/mp", "/slur", "/com", "/yawn", "/grat", "/hp", "/philippines", "/usa",
			"/indonesia", "/brazil", "/fsh", "/spin", "/sigh", "/dum", "/crwd", "/desp", "/dice"
		};
		int i;
		ARR_FIND( 0, ARRAYLENGTH(emo), i, stricmp(message, emo[i]) == 0 );
		if( i < ARRAYLENGTH(emo) )
		{
			clif_emotion(&pd->bl, i);
			return 0;
		}
	}

	snprintf(temp, sizeof temp ,"%s : %s", pd->pet.name, mes);
	clif_message(&pd->bl, temp);

	return 0;
}

/// @users - mostra o número de jogadores presentes em cada mapa (e porcentagem)
/// #users mostra no alvo em vez de em si mesmo.
ACMD_FUNC(users)
{
	char buf[CHAT_SIZE_MAX];
	int i;
	int users[MAX_MAPINDEX];
	int users_all;
	struct s_mapiterator* iter;

	memset(users, 0, sizeof(users));
	users_all = 0;

	// conta usuários em cada mapa.
	iter = mapit_getallusers();
	for(;;)
	{
		struct map_session_data* sd2 = (struct map_session_data*)mapit_next(iter);
		if( sd2 == NULL )
			break;// sem mais usuários.

		if( sd2->mapindex >= MAX_MAPINDEX )
			continue;// mapindex inválido.

		if( users[sd2->mapindex] < INT_MAX ) ++users[sd2->mapindex];
		if( users_all < INT_MAX ) ++users_all;
	}
	mapit_free(iter);

	// mostra os resultados para cada mapa.
	for( i = 0; i < MAX_MAPINDEX; ++i )
	{
		if( users[i] == 0 )
			continue;// vazui

		safesnprintf(buf, sizeof(buf), "%s: %d (%.2f%%)", mapindex_id2name(i), users[i], (float)(100.0f*users[i]/users_all));
		clif_displaymessage(sd->fd, buf);
	}

	// mostra a soma de todos.
	safesnprintf(buf, sizeof(buf), "all: %d", users_all);
	clif_displaymessage(sd->fd, buf);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(reset)
{
	pc_resetstate(sd);
	pc_resetskill(sd,1);
	sprintf(atcmd_output, msg_txt(208), sd->status.name); // '%s' pontos de classe e atributo resetados!
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
ACMD_FUNC(summon)
{
	char name[NAME_LENGTH];
	int mob_id = 0;
	int duration = 0;
	struct mob_data *md;
	unsigned int tick=gettick();

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%23s %d", name, &duration) < 1)
	{
		clif_displaymessage(fd, "Por favor, entre com o nome de um monstro. (uso: @summon <nome_monstro> [duração]");
		return -1;
	}

	if (duration < 1)
		duration =1;
	else if (duration > 60)
		duration =60;
	
	if ((mob_id = atoi(name)) == 0)
		mob_id = mobdb_searchname(name);
	if(mob_id == 0 || mobdb_checkid(mob_id) == 0)
	{
		clif_displaymessage(fd, msg_txt(40));	// Nome ou ID de monstro inválido.
		return -1;
	}

	md = mob_once_spawn_sub(&sd->bl, sd->bl.m, -1, -1, "--ja--", mob_id, "");

	if(!md)
		return -1;
	
	md->master_id=sd->bl.id;
	md->special_state.ai=1;
	md->deletetimer=add_timer(tick+(duration*60000),mob_timer_delete,md->bl.id,0);
	clif_specialeffect(&md->bl,344,AREA);
	mob_spawn(md);
	sc_start4(&md->bl, SC_MODECHANGE, 100, 1, 0, MD_AGGRESSIVE, 0, 60000);
	clif_skill_poseffect(&sd->bl,AM_CALLHOMUN,1,md->bl.x,md->bl.y,tick);
	clif_displaymessage(fd, msg_txt(39));	// Todos os monstros foram criados!
	
	return 0;
}

/*==========================================
 * @adjcmdlvl por [MouseJstr]
 *
 * Útil em testes beta para permitir que jogadores tenham comandos GM por pouco tempo.
 *------------------------------------------*/
ACMD_FUNC(adjcmdlvl)
{
	int newlev, newremotelev;
	char name[100];
	AtCommandInfo* cmd;

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %d %99s", &newlev, &newremotelev, name) != 3)
	{
		clif_displaymessage(fd, "uso: @adjcmdlvl <lvl> <nível remoto> <comando>.");
		return -1;
	}

	cmd = get_atcommandinfo_byname(name);
	if (cmd == NULL)
	{
		clif_displaymessage(fd, "@comando não encontrado.");
		return -1;
	}
	else if (newlev > pc_isGM(sd) || newremotelev > pc_isGM(sd) )
	{
		clif_displaymessage(fd, "Você não pode fazer um comando que requere um nível de GM maior que o seu.");
		return -1;
	}
	else if (cmd->level > pc_isGM(sd) || cmd->level2 > pc_isGM(sd) )
	{
		clif_displaymessage(fd, "Você não pode fazer um comando que requere um nível de GM maior que o seu.");
		return -1;
	}
	else
	{
		cmd->level = newlev;
		cmd->level2 = newremotelev;
		clif_displaymessage(fd, "Nível de @comando alterado.");
		return 0;
	}
}

/*==========================================
 * @adjgmlvl por [MouseJstr]
 * Cria um GM temporário
 * Útil em testes beta para permitir que jogadores tenham comandos GM por pouco tempo.
 *------------------------------------------*/
ACMD_FUNC(adjgmlvl)
{
	int newlev;
	char user[NAME_LENGTH];
	struct map_session_data *pl_sd;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %23[^\r\n]", &newlev, user) != 2) {
		clif_displaymessage(fd, "uso: @adjgmlvl <nível> <usuário>.");
		return -1;
	}

	if ( (pl_sd = map_nick2sd(user)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}

	pl_sd->gmlevel = newlev;

    return 0;
}

/*==========================================
 * @trade por [MouseJstr]
 * Abre uma janela de negociação com um jogador.
 *------------------------------------------*/
ACMD_FUNC(trade)
{
    struct map_session_data *pl_sd = NULL;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um jogador. (uso: @trade <jogador>).");
		return -1;
	}

	if ( (pl_sd = map_nick2sd((char *)message)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}

	trade_traderequest(sd, pl_sd);
	return 0;
}

/*==========================================
 * @setbattleflag por [MouseJstr]
 * define um flah battle_config sem ter que reiniciar.
 *------------------------------------------*/
ACMD_FUNC(setbattleflag)
{
	char flag[128], value[128];
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%127s %127s", flag, value) != 2) {
        	clif_displaymessage(fd, "uso: @setbattleflag <flag> <valor>.");
        	return -1;
    	}

	if (battle_set_value(flag, value) == 0)
	{
		clif_displaymessage(fd, "Flag battle_config desconhecida.");
		return -1;
	}

	clif_displaymessage(fd, "battle_config foi definida.");

	return 0;
}

/*==========================================
 * @unmute [Valaris]
 *------------------------------------------*/
ACMD_FUNC(unmute)
{
	struct map_session_data *pl_sd = NULL;
	nullpo_retr(-1, sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome de um jogador. (uso: @unmute <jogador>).");
		return -1;
	}

	if ( (pl_sd = map_nick2sd((char *)message)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}

	if(!pl_sd->sc.data[SC_NOCHAT]) {
		clif_displaymessage(sd->fd,"O jogador não está mudo.");
		return -1;
	}

	pl_sd->status.manner = 0;
	status_change_end(&pl_sd->bl, SC_NOCHAT, INVALID_TIMER);
	clif_displaymessage(sd->fd,"O jogador não está mais mudo.");
	
	return 0;
}

/*==========================================
 * @uptime por MC Cameri
 *------------------------------------------*/
ACMD_FUNC(uptime)
{
	unsigned long seconds = 0, day = 24*60*60, hour = 60*60,
		minute = 60, days = 0, hours = 0, minutes = 0;
	nullpo_retr(-1, sd);

	seconds = get_uptime();
	days = seconds/day;
	seconds -= (seconds/day>0)?(seconds/day)*day:0;
	hours = seconds/hour;
	seconds -= (seconds/hour>0)?(seconds/hour)*hour:0;
	minutes = seconds/minute;
	seconds -= (seconds/minute>0)?(seconds/minute)*minute:0;

	snprintf(atcmd_output, sizeof(atcmd_output), msg_txt(245), days, hours, minutes, seconds);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

/*==========================================
 * @changesex <sexo>
 * => Muda o sexo de alguém. O argumento sexo pode ser 0 ou 1, m ou f, male ou female.
 *------------------------------------------*/
ACMD_FUNC(changesex)
{
	nullpo_retr(-1, sd);
	chrif_changesex(sd);
	return 0;
}

/*================================================
 * @mute - Deixa o jogador mudo por tempo definido.
 *------------------------------------------------*/
ACMD_FUNC(mute)
{
	struct map_session_data *pl_sd = NULL;
	int manner;
	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%d %23[^\n]", &manner, atcmd_player_name) < 1) {
		clif_displaymessage(fd, "uso: @mute <tempo> <jogador>.");
		return -1;
	}

	if ( (pl_sd = map_nick2sd(atcmd_player_name)) == NULL )
	{
		clif_displaymessage(fd, msg_txt(3)); // Personagem não encontrado.
		return -1;
	}

	if ( pc_isGM(sd) < pc_isGM(pl_sd) )
	{
		clif_displaymessage(fd, msg_txt(81)); // Seu nível de GM não te autoriza a fazer esta ação neste jogador.
		return -1;
	}

	clif_manner_message(sd, 0);
	clif_manner_message(pl_sd, 5);

	if( pl_sd->status.manner < manner ) {
		pl_sd->status.manner -= manner;
		sc_start(&pl_sd->bl,SC_NOCHAT,100,0,0);
	} else {
		pl_sd->status.manner = 0;
		status_change_end(&pl_sd->bl, SC_NOCHAT, INVALID_TIMER);
	}

	clif_GM_silence(sd, pl_sd, (manner > 0 ? 1 : 0));

	return 0;
}

/*==========================================
 * @refresh (como @jumpto <<você mesmo>>)
 *------------------------------------------*/
ACMD_FUNC(refresh)
{
	nullpo_retr(-1, sd);
	clif_refresh(sd);
	return 0;
}

/*==========================================
 * @identify
 * => Lupa de GMs
 *------------------------------------------*/
ACMD_FUNC(identify)
{
	int i,num;

	nullpo_retr(-1, sd);

	for(i=num=0;i<MAX_INVENTORY;i++){
		if(sd->status.inventory[i].nameid > 0 && sd->status.inventory[i].identify!=1){
			num++;
		}
	}
	if (num > 0) {
		clif_item_identify_list(sd);
	} else {
		clif_displaymessage(fd,"Não há itens para avaliar.");
	}
	return 0;
}

/*==========================================
 * @gmotd (Global MOTD)
 * por davidsiaw :P
 *------------------------------------------*/
ACMD_FUNC(gmotd)
{
	char buf[CHAT_SIZE_MAX];
	size_t len;
	FILE* fp;

	if( ( fp = fopen(motd_txt, "r") ) != NULL )
	{
		while( fgets(buf, sizeof(buf), fp) )
		{
			if( buf[0] == '/' && buf[1] == '/' )
			{
				continue;
			}

			len = strlen(buf);

			while( len && ( buf[len-1] == '\r' || buf[len-1] == '\n' ) )
			{
				len--;
			}

			if( len )
			{
				buf[len] = 0;

				intif_broadcast(buf, len+1, 0);
			}
		}
		fclose(fp);
	}
	return 0;
}

ACMD_FUNC(misceffect)
{
	int effect = 0;
	nullpo_retr(-1, sd);
	if (!message || !*message)
		return -1;
	if (sscanf(message, "%d", &effect) < 1)
		return -1;
	clif_misceffect(&sd->bl,effect);

	return 0;
}

/*==========================================
 * sistema de mail.
 *------------------------------------------*/
ACMD_FUNC(mail)
{
	nullpo_ret(sd);
#ifndef TXT_ONLY
	mail_openmail(sd);
#endif
	return 0;
}

/*==========================================
 * Mostra informações do monstro   v 1.0
 * originalmente por [Lupus] eAthena
 *------------------------------------------*/
ACMD_FUNC(mobinfo)
{
	unsigned char msize[3][7] = {"Small", "Medium", "Large"};
	unsigned char mrace[12][11] = {"Formless", "Undead", "Beast", "Plant", "Insect", "Fish", "Demon", "Demi-Human", "Angel", "Dragon", "Boss", "Non-Boss"};
	unsigned char melement[10][8] = {"Neutral", "Water", "Earth", "Fire", "Wind", "Poison", "Holy", "Dark", "Ghost", "Undead"};
	char atcmd_output2[CHAT_SIZE_MAX];
	struct item_data *item_data;
	struct mob_db *mob, *mob_array[MAX_SEARCH];
	int count;
	int i, j, k;

	memset(atcmd_output, '\0', sizeof(atcmd_output));
	memset(atcmd_output2, '\0', sizeof(atcmd_output2));

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome/id de um monstro. (uso: @mobinfo <nome/ID>).");
		return -1;
	}

	// Se o argumento é um nome.
	if ((i = mobdb_checkid(atoi(message))))
	{
		mob_array[0] = mob_db(i);
		count = 1;
	} else
		count = mobdb_searchname_array(mob_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(40)); // Nome ou ID de monstro inválido.
		return -1;
	}

	if (count > MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, count);
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}
	for (k = 0; k < count; k++) {
		mob = mob_array[k];

		// stats
		if (mob->mexp)
			sprintf(atcmd_output, "Monstro MVP: '%s'/'%s'/'%s' (%d)", mob->name, mob->jname, mob->sprite, mob->vd.class_);
		else
			sprintf(atcmd_output, "Monstro: '%s'/'%s'/'%s' (%d)", mob->name, mob->jname, mob->sprite, mob->vd.class_);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, " Nível:%d  HP:%d  SP:%d  Exp de Base:%u  Exp de classe:%u", mob->lv, mob->status.max_hp, mob->status.max_sp, mob->base_exp, mob->job_exp);
		clif_displaymessage(fd, atcmd_output);
		sprintf(atcmd_output, " DEF:%d  MDEF:%d  FOR:%d  AGI:%d  VIT:%d  INT:%d  DES:%d  SOR:%d",
			mob->status.def, mob->status.mdef, mob->status.str, mob->status.agi,
			mob->status.vit, mob->status.int_, mob->status.dex, mob->status.luk);
		clif_displaymessage(fd, atcmd_output);
		
		sprintf(atcmd_output, " ATK:%d~%d  Alcance:%d~%d~%d  Tamanho:%s  Raça: %s  Elemento: %s (Lv:%d)",
			mob->status.rhw.atk, mob->status.rhw.atk2, mob->status.rhw.range,
			mob->range2 , mob->range3, msize[mob->status.size],
			mrace[mob->status.race], melement[mob->status.def_ele], mob->status.ele_lv);
		clif_displaymessage(fd, atcmd_output);
		// drops
		clif_displaymessage(fd, " Drops:");
		strcpy(atcmd_output, " ");
		j = 0;
		for (i = 0; i < MAX_MOB_DROP; i++) {
			if (mob->dropitem[i].nameid <= 0 || mob->dropitem[i].p < 1 || (item_data = itemdb_exists(mob->dropitem[i].nameid)) == NULL)
				continue;
			if (item_data->slot)
				sprintf(atcmd_output2, " - %s[%d]  %02.02f%%", item_data->jname, item_data->slot, (float)mob->dropitem[i].p / 100);
			else
				sprintf(atcmd_output2, " - %s  %02.02f%%", item_data->jname, (float)mob->dropitem[i].p / 100);
			strcat(atcmd_output, atcmd_output2);
			if (++j % 3 == 0) {
				clif_displaymessage(fd, atcmd_output);
				strcpy(atcmd_output, " ");
			}
		}
		if (j == 0)
			clif_displaymessage(fd, "Este monstro não dropa nada.");
		else if (j % 3 != 0)
			clif_displaymessage(fd, atcmd_output);
		// mvp
		if (mob->mexp) {
			sprintf(atcmd_output, " MVP Bonus EXP:%u  %02.02f%%", mob->mexp, (float)mob->mexpper / 100);
			clif_displaymessage(fd, atcmd_output);
			strcpy(atcmd_output, " MVP Itens:");
			j = 0;
			for (i = 0; i < 3; i++) {
				if (mob->mvpitem[i].nameid <= 0 || (item_data = itemdb_exists(mob->mvpitem[i].nameid)) == NULL)
					continue;
				if (mob->mvpitem[i].p > 0) {
					j++;
					if (j == 1)
						sprintf(atcmd_output2, " %s  %02.02f%%", item_data->name, (float)mob->mvpitem[i].p / 100);
					else
						sprintf(atcmd_output2, " - %s  %02.02f%%", item_data->name, (float)mob->mvpitem[i].p / 100);
					strcat(atcmd_output, atcmd_output2);
				}
			}
			if (j == 0)
				clif_displaymessage(fd, "Este monstro não tem nenhum prêmio de MVP.");
			else
				clif_displaymessage(fd, atcmd_output);
		}
	}
	return 0;
}

/*=========================================
* @showmobs por KarLaeda
* => Mostra os monstros no mini mapa por 5 segundos.
*------------------------------------------*/
int atshowmobs_timer(int tid, unsigned int tick, int id, intptr data)
{
	struct map_session_data* sd = map_id2sd(id);
	if( sd == NULL )
		return 0;

	// remove indicador
	clif_viewpoint(sd, 1, 2, 0, 0, (int)data, 0xFFFFFF);
	return 1;
}

ACMD_FUNC(showmobs)
{
	char mob_name[100];
	int mob_id;
	int number = 0;
	struct s_mapiterator* it;

	nullpo_retr(-1, sd);

	if(sscanf(message, "%99[^\n]", mob_name) < 0)
		return -1;

	if((mob_id = atoi(mob_name)) == 0)
		mob_id = mobdb_searchname(mob_name);
	if(mob_id > 0 && mobdb_checkid(mob_id) == 0){
		snprintf(atcmd_output, sizeof atcmd_output, "ID %s inválido.!",mob_name);
		clif_displaymessage(fd, atcmd_output);
		return 0;
	}
// Uncomment the following line to show mini-bosses & MVP.
//#define SHOW_MVP
#ifndef SHOW_MVP
	if(mob_db(mob_id)->status.mode&MD_BOSS){
		snprintf(atcmd_output, sizeof atcmd_output, "Monstros do tipo chefe não são mostrados.");
		clif_displaymessage(fd, atcmd_output);
		return 0;
	}
#endif
	if(mob_id == atoi(mob_name) && mob_db(mob_id)->jname)
		strcpy(mob_name,mob_db(mob_id)->jname);    // --ja--
		//strcpy(mob_name,mob_db(mob_id)->name);    // --en--

	snprintf(atcmd_output, sizeof atcmd_output, "Procurando monstro... %s %s",
		mob_name, mapindex_id2name(sd->mapindex));
	clif_displaymessage(fd, atcmd_output);

	it = mapit_geteachmob();
	for(;;)
	{
		TBL_MOB* md = (TBL_MOB*)mapit_next(it);
		if( md == NULL )
			break;// sem mais monstros.

		if( md->bl.m != sd->bl.m )
			continue;
		if( mob_id != -1 && md->class_ != mob_id )
			continue;
		if( md->special_state.ai || md->master_id )
			continue; // esconde mobs domados e summonados pelo jogador
		if( md->spawn_timer != INVALID_TIMER )
			continue; // esconde monstros esperando pelo respawn.

		++number;
		clif_viewpoint(sd, 1, 1, md->bl.x, md->bl.y, number, 0xFFFFFF);
		add_timer(gettick()+5000, atshowmobs_timer, sd->bl.id, number);
	}
	mapit_free(it);

	return 0;
}

/*==========================================
 * homunculus level up [orn]
 *------------------------------------------*/
ACMD_FUNC(homlevel)
{
	TBL_HOM * hd;
	int level = 0, i = 0;

	nullpo_retr(-1, sd);

	if ( !message || !*message || ( level = atoi(message) ) < 1 ) {
		clif_displaymessage(fd, "Por favor, entre com um ajuste de nível. (uso: @homlevel <níveis para aumentar>.");
		return -1;
	}

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "Você não tem homunculus.");
		return -1;
	}

	hd = sd->hd;

	for (i = 1; i <= level && hd->exp_next; i++){
		hd->homunculus.exp += hd->exp_next;
		merc_hom_levelup(hd);
	}
	status_calc_homunculus(hd,0);
	status_percent_heal(&hd->bl, 100, 100);
	clif_specialeffect(&hd->bl,568,AREA);
	return 0;
}

/*==========================================
 * evolução de homunculus H [orn]
 *------------------------------------------*/
ACMD_FUNC(homevolution)
{
	nullpo_retr(-1, sd);

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "Você não tem homunculus.");
		return -1;
	}

	if ( !merc_hom_evolution(sd->hd) ) {
		clif_displaymessage(fd, "O seu homunculus não evolui.");
		return -1;
	}

	return 0;
}

/*==========================================
 * chama o homunculus escolhido. [orn]
 *------------------------------------------*/
ACMD_FUNC(makehomun)
{
	int homunid;
	nullpo_retr(-1, sd);

	if ( sd->status.hom_id ) {
		clif_displaymessage(fd, msg_txt(450));
		return -1;
	}

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o ID de um homunculus. (uso: @makehomun <ID>.");
		return -1;
	}

	homunid = atoi(message);
	if( homunid < HM_CLASS_BASE || homunid > HM_CLASS_BASE + MAX_HOMUNCULUS_CLASS - 1 )
	{
		clif_displaymessage(fd, "ID de homunculus inválido.");
		return -1;
	}

	merc_create_homunculus_request(sd,homunid);
	return 0;
}

/*==========================================
 * Modifica intimidade de homunculus [orn]
 *------------------------------------------*/
ACMD_FUNC(homfriendly)
{
	int friendly = 0;

	nullpo_retr(-1, sd);

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "Você não tem um homunculus.");
		return -1;
	}

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o valor de amizade. (uso: @homfriendly <valor de amizade [de 0-1000]>.");
		return -1;
	}

	friendly = atoi(message);
	friendly = cap_value(friendly, 0, 1000);

	sd->hd->homunculus.intimacy = friendly * 100 ;
	clif_send_homdata(sd,SP_INTIMATE,friendly);
	return 0;
}

/*==========================================
 * modifica a fome do homunculus. [orn]
 *------------------------------------------*/
ACMD_FUNC(homhungry)
{
	int hungry = 0;

	nullpo_retr(-1, sd);

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "Você não tem um homunculus.");
		return -1;
	}

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com um valor de fome. (uso: @homhungry <valor de fome[de 0-100]>.");
		return -1;
	}

	hungry = atoi(message);
	hungry = cap_value(hungry, 0, 100);

	sd->hd->homunculus.hunger = hungry;
	clif_send_homdata(sd,SP_HUNGRY,hungry);
	return 0;
}

/*==========================================
 * faz o homunculus falar [orn]
 *------------------------------------------*/
ACMD_FUNC(homtalk)
{
	char mes[100],temp[100];

	nullpo_retr(-1, sd);

	if (sd->sc.count && //sem chat enquanto mudo
		(sd->sc.data[SC_BERSERK] ||
		(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCHAT) ||
		(sd->sc.data[SC_DEEPSLEEP] && sd->sc.data[SC_DEEPSLEEP]->val2) ||
		sd->sc.data[SC_SATURDAYNIGHTFEVER]))
		return -1;

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "Você não tem um homunculus.");
		return -1;
	}

	if (!message || !*message || sscanf(message, "%99[^\n]", mes) < 1) {
		clif_displaymessage(fd, "Por favor, entre com uma mensagem. (uso: @homtalk <mensagem>");
		return -1;
	}

	snprintf(temp, sizeof temp ,"%s : %s", sd->hd->homunculus.name, mes);
	clif_message(&sd->hd->bl, temp);

	return 0;
}

/*==========================================
 * Mostra o status do homunculus.
 *------------------------------------------*/
ACMD_FUNC(hominfo)
{
	struct homun_data *hd;
	struct status_data *status;
	nullpo_retr(-1, sd);

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "Você não tem um homunculus.");
		return -1;
	}

	hd = sd->hd;
	status = status_get_status_data(&hd->bl);
	clif_displaymessage(fd, "Status do homunculus :");

	snprintf(atcmd_output, sizeof(atcmd_output) ,"HP : %d/%d - SP : %d/%d",
		status->hp, status->max_hp, status->sp, status->max_sp);
	clif_displaymessage(fd, atcmd_output);

	snprintf(atcmd_output, sizeof(atcmd_output) ,"ATK : %d - MATK : %d~%d",
		status->rhw.atk2 +status->batk, status->matk_min, status->matk_max);
	clif_displaymessage(fd, atcmd_output);

	snprintf(atcmd_output, sizeof(atcmd_output) ,"Fome : %d - Intimidade : %u",
		hd->homunculus.hunger, hd->homunculus.intimacy/100);
	clif_displaymessage(fd, atcmd_output);

	snprintf(atcmd_output, sizeof(atcmd_output) ,
		"Status: For %d / Agi %d / Vit %d / Int %d / Des %d / Sor %d",
		status->str, status->agi, status->vit,
		status->int_, status->dex, status->luk);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

ACMD_FUNC(homstats)
{
	struct homun_data *hd;
	struct s_homunculus_db *db;
	struct s_homunculus *hom;
	int lv, min, max, evo;

	nullpo_retr(-1, sd);

	if ( !merc_is_hom_active(sd->hd) ) {
		clif_displaymessage(fd, "Você não tem um homunculus.");
		return -1;
	}

	hd = sd->hd;
	
	hom = &hd->homunculus;
	db = hd->homunculusDB;
	lv = hom->level;

	snprintf(atcmd_output, sizeof(atcmd_output) ,
		"Status de crescimento de homunculus (Lv %d %s):", lv, db->name);
	clif_displaymessage(fd, atcmd_output);
	lv--; 
	
	evo = (hom->class_ == db->evo_class);
	min = db->base.HP +lv*db->gmin.HP +(evo?db->emin.HP:0);
	max = db->base.HP +lv*db->gmax.HP +(evo?db->emax.HP:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Max HP: %d (%d~%d)", hom->max_hp, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.SP +lv*db->gmin.SP +(evo?db->emin.SP:0);
	max = db->base.SP +lv*db->gmax.SP +(evo?db->emax.SP:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Max SP: %d (%d~%d)", hom->max_sp, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.str +lv*(db->gmin.str/10) +(evo?db->emin.str:0);
	max = db->base.str +lv*(db->gmax.str/10) +(evo?db->emax.str:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"For: %d (%d~%d)", hom->str/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.agi +lv*(db->gmin.agi/10) +(evo?db->emin.agi:0);
	max = db->base.agi +lv*(db->gmax.agi/10) +(evo?db->emax.agi:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Agi: %d (%d~%d)", hom->agi/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.vit +lv*(db->gmin.vit/10) +(evo?db->emin.vit:0);
	max = db->base.vit +lv*(db->gmax.vit/10) +(evo?db->emax.vit:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Vit: %d (%d~%d)", hom->vit/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.int_ +lv*(db->gmin.int_/10) +(evo?db->emin.int_:0);
	max = db->base.int_ +lv*(db->gmax.int_/10) +(evo?db->emax.int_:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Int: %d (%d~%d)", hom->int_/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.dex +lv*(db->gmin.dex/10) +(evo?db->emin.dex:0);
	max = db->base.dex +lv*(db->gmax.dex/10) +(evo?db->emax.dex:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Des: %d (%d~%d)", hom->dex/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	min = db->base.luk +lv*(db->gmin.luk/10) +(evo?db->emin.luk:0);
	max = db->base.luk +lv*(db->gmax.luk/10) +(evo?db->emax.luk:0);;
	snprintf(atcmd_output, sizeof(atcmd_output) ,"Sor: %d (%d~%d)", hom->luk/10, min, max);
	clif_displaymessage(fd, atcmd_output);

	return 0;
}

ACMD_FUNC(homshuffle)
{
	nullpo_retr(-1, sd);

	if(!sd->hd)
		return -1; // nada a fazer

	if(!merc_hom_shuffle(sd->hd))
		return -1;

	clif_displaymessage(sd->fd, "[Status do homunculus alterado]");
	atcommand_homstats(fd, sd, command, message); //Mostra os novos status.
	return 0;
}

/*==========================================
 * Mostra informações de itens   v 1.0
 * originalmente por [Lupus] eAthena
 *------------------------------------------*/
ACMD_FUNC(iteminfo)
{
	struct item_data *item_data, *item_array[MAX_SEARCH];
	int i, count = 1;

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome ou ID de um item.(uso: @ii/@iteminfo <nome/ID>).");
		return -1;
	}
	if ((item_array[0] = itemdb_exists(atoi(message))) == NULL)
		count = itemdb_searchname_array(item_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(19));	// Nome/ID inválido
		return -1;
	}

	if (count > MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, count); // Mostrando o primeiro %d dos %d resultados
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}
	for (i = 0; i < count; i++) {
		item_data = item_array[i];
		sprintf(atcmd_output, "Item: '%s'/'%s'[%d] (%d) Tipo: %s | Efeito extra: %s",
			item_data->name,item_data->jname,item_data->slot,item_data->nameid,
			itemdb_typename(item_data->type), 
			(item_data->script==NULL)? "Nenhum" : "Com script"
		);
		clif_displaymessage(fd, atcmd_output);

		sprintf(atcmd_output, "Compra em NPC:%dz, Venda:%dz | Peso: %.1f ", item_data->value_buy, item_data->value_sell, item_data->weight/10. );
		clif_displaymessage(fd, atcmd_output);

		if (item_data->maxchance == -1)
			strcpy(atcmd_output, " - Apenas disponível em lojas.");
		else if (item_data->maxchance)
			sprintf(atcmd_output, " - Chance máxima de drop por monstros: %02.02f%%", (float)item_data->maxchance / 100 );
		else
			strcpy(atcmd_output, " - Monstros não dropam este item.");
		clif_displaymessage(fd, atcmd_output);

	}
	return 0;
}

/*==========================================
 * Mostra quem dropa o item.
 *------------------------------------------*/
ACMD_FUNC(whodrops)
{
	struct item_data *item_data, *item_array[MAX_SEARCH];
	int i,j, count = 1;

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome ou ID de um item. (uso: @whodrops <nome/ID>).");
		return -1;
	}
	if ((item_array[0] = itemdb_exists(atoi(message))) == NULL)
		count = itemdb_searchname_array(item_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(19));	// Nome/ID inválido.
		return -1;
	}

	if (count > MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, count); // Mostrando o primeiro %d dos %d resultados
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}
	for (i = 0; i < count; i++) {
		item_data = item_array[i];
		sprintf(atcmd_output, "Item: '%s'[%d]", item_data->jname,item_data->slot);
		clif_displaymessage(fd, atcmd_output);

		if (item_data->mob[0].chance == 0) {
			strcpy(atcmd_output, " - Este item não é dropado por monstros.");
			clif_displaymessage(fd, atcmd_output);
		} else {
			sprintf(atcmd_output, "- Monstros comuns com a mais alta chance de drop (apenas max %d são listadas):", MAX_SEARCH);
			clif_displaymessage(fd, atcmd_output);
		
			for (j=0; j < MAX_SEARCH && item_data->mob[j].chance > 0; j++)
			{
				sprintf(atcmd_output, "- %s (%02.02f%%)", mob_db(item_data->mob[j].id)->jname, item_data->mob[j].chance/100.);
				clif_displaymessage(fd, atcmd_output);
			}
		}
	}
	return 0;
}

ACMD_FUNC(whereis)
{
	struct mob_db *mob, *mob_array[MAX_SEARCH];
	int count;
	int i, j, k;

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o nome ou ID de um monstro. (uso: @whereis <nome/ID>).");
		return -1;
	}

	// Se o argumento for um nome.
	if ((i = mobdb_checkid(atoi(message))))
	{
		mob_array[0] = mob_db(i);
		count = 1;
	} else
		count = mobdb_searchname_array(mob_array, MAX_SEARCH, message);

	if (!count) {
		clif_displaymessage(fd, msg_txt(40)); // Nome/ID do monstro inválido.
		return -1;
	}

	if (count > MAX_SEARCH) {
		sprintf(atcmd_output, msg_txt(269), MAX_SEARCH, count);
		clif_displaymessage(fd, atcmd_output);
		count = MAX_SEARCH;
	}
	for (k = 0; k < count; k++) {
		mob = mob_array[k];
		snprintf(atcmd_output, sizeof atcmd_output, "%s nasce em:", mob->jname);
		clif_displaymessage(fd, atcmd_output);

		for (i = 0; i < ARRAYLENGTH(mob->spawn) && mob->spawn[i].qty; i++)
		{
			j = map_mapindex2mapid(mob->spawn[i].mapindex);
			if (j < 0) continue;
			snprintf(atcmd_output, sizeof atcmd_output, "%s (%d)", map[j].name, mob->spawn[i].qty);
			clif_displaymessage(fd, atcmd_output);
		}
		if (i == 0)
			clif_displaymessage(fd, "Este monstro não nasce normalmente.");
	}

	return 0;
}

/*==========================================
 * @adopt por [Veider]
 * adota um aprendiz
 *------------------------------------------*/
ACMD_FUNC(adopt)
{
	struct map_session_data *pl_sd1, *pl_sd2, *pl_sd3;
	char player1[NAME_LENGTH], player2[NAME_LENGTH], player3[NAME_LENGTH];
	char output[CHAT_SIZE_MAX];

	nullpo_retr(-1, sd);

	if (!message || !*message || sscanf(message, "%23[^,],%23[^,],%23[^\r\n]", player1, player2, player3) < 3) {
		clif_displaymessage(fd, "uso: @adopt <pai>,<mãe>,<filho>.");
		return -1;
	}

	if (battle_config.etc_log)
		ShowInfo("Adotando: --%s--%s--%s--\n",player1,player2,player3);

	if((pl_sd1=map_nick2sd((char *) player1)) == NULL) {
		sprintf(output, "Não se pode achar o jogador %s online.", player1);
		clif_displaymessage(fd, output);
		return -1;
	}

	if((pl_sd2=map_nick2sd((char *) player2)) == NULL) {
		sprintf(output, "Não se pode achar o jogador %s online.", player2);
		clif_displaymessage(fd, output);
		return -1;
	}
 
	if((pl_sd3=map_nick2sd((char *) player3)) == NULL) {
		sprintf(output, "Não se pode achar o jogador %s online.", player3);
		clif_displaymessage(fd, output);
		return -1;
	}

	if( !pc_adoption(pl_sd1, pl_sd2, pl_sd3) ) {
		return -1;
	}
	
	clif_displaymessage(fd, "Eles são uma família agora... deseje-os sorte.");
	return 0;
}

ACMD_FUNC(version)
{
	const char * revision;

	if ((revision = get_svn_revision()) != 0) {
		sprintf(atcmd_output,"Versão do SVN Cronus r%s",revision);
		clif_displaymessage(fd,atcmd_output);
	} else 
		clif_displaymessage(fd,"Não se pode determinar a versão do SVN.");

	return 0;
}

/*==========================================
 * @mutearea por MouseJstr
 *------------------------------------------*/
static int atcommand_mutearea_sub(struct block_list *bl,va_list ap)
{
	
	int time, id;
	struct map_session_data *pl_sd = (struct map_session_data *)bl;
	if (pl_sd == NULL)
		return 0;

	id = va_arg(ap, int);
	time = va_arg(ap, int);

	if (id != bl->id && !pc_isGM(pl_sd)) {
		pl_sd->status.manner -= time;
		if (pl_sd->status.manner < 0)
			sc_start(&pl_sd->bl,SC_NOCHAT,100,0,0);
		else
			status_change_end(&pl_sd->bl, SC_NOCHAT, INVALID_TIMER);
	}
	return 0;
}

ACMD_FUNC(mutearea)
{
	int time;
	nullpo_ret(sd);

	if (!message || !*message) {
		clif_displaymessage(fd, "Por favor, entre com o tempo em minutos. (uso: @mutearea/@stfu <tempo em minutos>.");
		return -1;
	}
	
	time = atoi(message);

	map_foreachinarea(atcommand_mutearea_sub,sd->bl.m, 
		sd->bl.x-AREA_SIZE, sd->bl.y-AREA_SIZE, 
		sd->bl.x+AREA_SIZE, sd->bl.y+AREA_SIZE, BL_PC, sd->bl.id, time);

	return 0;
}


ACMD_FUNC(rates)
{
	char buf[CHAT_SIZE_MAX];
	
	nullpo_ret(sd);
	memset(buf, '\0', sizeof(buf));
	
	snprintf(buf, CHAT_SIZE_MAX, "Rates de experiência: Base %.2fx / Classe %.2fx",
		battle_config.base_exp_rate/100., battle_config.job_exp_rate/100.);
	clif_displaymessage(fd, buf);
	snprintf(buf, CHAT_SIZE_MAX, "Rates de drop normais : Comuns %.2fx / Curadores %.2fx / Utilizáveis %.2fx / Equipamento %.2fx / Cartas %.2fx",
		battle_config.item_rate_common/100., battle_config.item_rate_heal/100., battle_config.item_rate_use/100., battle_config.item_rate_equip/100., battle_config.item_rate_card/100.);
	clif_displaymessage(fd, buf);
	snprintf(buf, CHAT_SIZE_MAX, "Rates de drop de chefes: Comuns %.2fx / Curadores %.2fx / Utilizáveis %.2fx / Equipamentos %.2fx / Cartas %.2fx",
		battle_config.item_rate_common_boss/100., battle_config.item_rate_heal_boss/100., battle_config.item_rate_use_boss/100., battle_config.item_rate_equip_boss/100., battle_config.item_rate_card_boss/100.);
	clif_displaymessage(fd, buf);
	snprintf(buf, CHAT_SIZE_MAX, "Outras rates de drop: MvP %.2fx / Baseado-carta %.2fx / Tesouro %.2fx",
		battle_config.item_rate_mvp/100., battle_config.item_rate_adddrop/100., battle_config.item_rate_treasure/100.);
	clif_displaymessage(fd, buf);
	
	return 0;
}

/*==========================================
 * @me por lordalfa
 * => Mostra a string de SAÍDA em cima da cabeça dos jogadores visíveis.
 *------------------------------------------*/
ACMD_FUNC(me)
{
	char tempmes[CHAT_SIZE_MAX];
	nullpo_retr(-1, sd);

	memset(tempmes, '\0', sizeof(tempmes));
	memset(atcmd_output, '\0', sizeof(atcmd_output));

	if (sd->sc.count && //sem conversa quando mudo
		(sd->sc.data[SC_BERSERK] ||
		(sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCHAT) ||
		(sd->sc.data[SC_DEEPSLEEP] && sd->sc.data[SC_DEEPSLEEP]->val2) ||
		sd->sc.data[SC_SATURDAYNIGHTFEVER]))
		return -1;

	if (!message || !*message || sscanf(message, "%199[^\n]", tempmes) < 0) {
		clif_displaymessage(fd, "Por favor, entre com uma mensagem (uso: @me <mensagem>).");
		return -1;
	}
	
	sprintf(atcmd_output, msg_txt(270), sd->status.name, tempmes);	// *%s %s*
	clif_disp_overhead(sd, atcmd_output);
	
	return 0;
	
}

/*==========================================
 * @size
 * => Redimensiona a sprite do seu personagem. [Valaris]
 *------------------------------------------*/
ACMD_FUNC(size)
{
	int size=0;

	nullpo_retr(-1, sd);

	size = atoi(message);
	if(sd->state.size) {
		sd->state.size=0;
		pc_setpos(sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_TELEPORT);
	}

	if(size==1) {
		sd->state.size=1;
		clif_specialeffect(&sd->bl,420,AREA);
	} else if(size==2) {
		sd->state.size=2;
		clif_specialeffect(&sd->bl,422,AREA);
	}

	return 0;
}

/*==========================================
 * @monsterignore
 * => Faz os monstros ignorarem você. [Valaris]
 *------------------------------------------*/
ACMD_FUNC(monsterignore)
{
	nullpo_retr(-1, sd);

	if (!sd->state.monster_ignore) {
		sd->state.monster_ignore = 1;
		clif_displaymessage(sd->fd, "Você está imune à ataques.");
	} else {
		sd->state.monster_ignore = 0;
		clif_displaymessage(sd->fd, "Você voltou ao normal.");
	}

	return 0;
}
/*==========================================
 * @fakename
 * => Da um nome falso para o seu personagem. [Valaris]
 *------------------------------------------*/
ACMD_FUNC(fakename)
{
	nullpo_retr(-1, sd);

	if( !message || !*message )
	{
		if( sd->fakename[0] )
		{
			sd->fakename[0] = '\0';
			clif_charnameack(0, &sd->bl);
			clif_displaymessage(sd->fd, "Seu nome voltou a ser o original.");
			return 0;
		}

		clif_displaymessage(sd->fd, "Você precisa entrar com um nome.");
		return -1;
	}

	if( strlen(message) < 2 )
	{
		clif_displaymessage(sd->fd, "O nome falso precisa ter pelo menos dois caracteres.");
		return -1;
	}
	
	safestrncpy(sd->fakename, message, sizeof(sd->fakename));
	clif_charnameack(0, &sd->bl);
	clif_displaymessage(sd->fd, "Nome falso habilitado.");

	return 0;
}

/*==========================================
 * @mapflag [nome do flag] [1|0|on|off] [nome do mapa] por Lupus
 * => Mostra informações sobre os mapflags[nome do mapa]
 * Também define mapflags.
 * Em construção.
 *------------------------------------------*/
ACMD_FUNC(mapflag)
{
// WIP
	return 0;
}

/*===================================
 * Remove algumas mensagens.
 *-----------------------------------*/
ACMD_FUNC(showexp)
{
	if (sd->state.showexp) {
		sd->state.showexp = 0;
		clif_displaymessage(fd, "O ganho de experiência não será mostrado.");
		return 0;
	}

	sd->state.showexp = 1;
	clif_displaymessage(fd, "O ganho de experiência será mostrado.");
	return 0;
}

ACMD_FUNC(showzeny)
{
	if (sd->state.showzeny) {
		sd->state.showzeny = 0;
		clif_displaymessage(fd, "O ganho de zeny não será mostrado.");
		return 0;
	}

	sd->state.showzeny = 1;
	clif_displaymessage(fd, "O ganho de zeny será mostrado.");
	return 0;
}

ACMD_FUNC(showdelay)
{
	if (sd->state.showdelay) {
		sd->state.showdelay = 0;
		clif_displaymessage(fd, "Falhas no delay das skills não serão mostradas.");
		return 0;
	}
	
	sd->state.showdelay = 1;
	clif_displaymessage(fd, "Falhas no delay das skills serão mostradas..");
	return 0;
}

/*==========================================
 * Funções de organização de duelo. [LuzZza]
 *
 * @duel [limite|nick] - cria um duelo
 * @invite <nick> - convida um jogador
 * @accept - aceita o convite
 * @reject - rejeita o convite
 * @leave - sai de um duelo
 *------------------------------------------*/
ACMD_FUNC(invite)
{
	unsigned int did = sd->duel_group;
	struct map_session_data *target_sd = map_nick2sd((char *)message);

	if(did <= 0)	{
		// "Duelo: @invite sem @duel."
		clif_displaymessage(fd, msg_txt(350));
		return 0;
	}
	
	if(duel_list[did].max_players_limit > 0 &&
		duel_list[did].members_count >= duel_list[did].max_players_limit) {
		
		// "Duelo: Limite de Jogadores alcançado."
		clif_displaymessage(fd, msg_txt(351));
		return 0;
	}
	
	if(target_sd == NULL) {
		// "Duel: Jogador não encontrado."
		clif_displaymessage(fd, msg_txt(352));
		return 0;
	}
	
	if(target_sd->duel_group > 0 || target_sd->duel_invite > 0) {
		// "Duelo: O jogador já está em um duelo."
		clif_displaymessage(fd, msg_txt(353));
		return 0;
	}

	if(battle_config.duel_only_on_same_map && target_sd->bl.m != sd->bl.m)
	{
		sprintf(atcmd_output, msg_txt(364), message);
		clif_displaymessage(fd, atcmd_output);
		return 0;
	}
	
	duel_invite(did, sd, target_sd);
	// "Duelo: Convite enviado."
	clif_displaymessage(fd, msg_txt(354));
	return 0;
}

ACMD_FUNC(duel)
{
	char output[CHAT_SIZE_MAX];
	unsigned int maxpl=0, newduel;
	struct map_session_data *target_sd;

	if(sd->duel_group > 0) {
		duel_showinfo(sd->duel_group, sd);
		return 0;
	}

	if(sd->duel_invite > 0) {
		// Duelo: @duel sem @reject.
		clif_displaymessage(fd, msg_txt(355));
		return 0;
	}

	if(!duel_checktime(sd)) {
		// "Duelo: Você só poder duelar uma vez a cada %d minutos."
		sprintf(output, msg_txt(356), battle_config.duel_time_interval);
		clif_displaymessage(fd, output);
		return 0;
	}

	if( message[0] ) {
		if(sscanf(message, "%d", &maxpl) >= 1) {
			if(maxpl < 2 || maxpl > 65535) {
				clif_displaymessage(fd, msg_txt(357)); // "Duel: Valor inválido."
				return 0;
			}
			duel_create(sd, maxpl);
		} else {
			target_sd = map_nick2sd((char *)message);
			if(target_sd != NULL) {
				if((newduel = duel_create(sd, 2)) != -1) {
					if(target_sd->duel_group > 0 ||	target_sd->duel_invite > 0) {
						clif_displaymessage(fd, msg_txt(353)); // "Duelo: O jogador já está em um duelo."
						return 0;
					}
					duel_invite(newduel, sd, target_sd);
					clif_displaymessage(fd, msg_txt(354)); // Duelo: Convite enviado.
				}
			} else {
				// "Duel: Jogador não encontrado."
				clif_displaymessage(fd, msg_txt(352));
				return 0;
			}
		}
	} else
		duel_create(sd, 0);

	return 0;
}


ACMD_FUNC(leave)
{
	if(sd->duel_group <= 0) {
		// Duelo: @leave sem @duel.
		clif_displaymessage(fd, msg_txt(358));
		return 0;
	}

	duel_leave(sd->duel_group, sd);
	clif_displaymessage(fd, msg_txt(359)); // Duelo: Você deixou o duelo.
	return 0;
}

ACMD_FUNC(accept)
{
	char output[CHAT_SIZE_MAX];

	if(!duel_checktime(sd)) {
		// Duelo: Você só poder duelar uma vez a cada %d minutos.
		sprintf(output, msg_txt(356), battle_config.duel_time_interval);
		clif_displaymessage(fd, output);
		return 0;
	}

	if(sd->duel_invite <= 0) {
		// Duelo: @accept sem convite.
		clif_displaymessage(fd, msg_txt(360));
		return 0;
	}

	if( duel_list[sd->duel_invite].max_players_limit > 0 && duel_list[sd->duel_invite].members_count >= duel_list[sd->duel_invite].max_players_limit )
	{
		// Duelo: Limite de jogadores alcançado.
		clif_displaymessage(fd, msg_txt(351));
		return 0;
	}

	duel_accept(sd->duel_invite, sd);
	// Duelo: Convite aceito.
	clif_displaymessage(fd, msg_txt(361));
	return 0;
}

ACMD_FUNC(reject)
{
	if(sd->duel_invite <= 0) {
		// Duelo: @reject sem convite.
		clif_displaymessage(fd, msg_txt(362));
		return 0;
	}

	duel_reject(sd->duel_invite, sd);
	// Duelo: Convite rejeitado.
	clif_displaymessage(fd, msg_txt(363));
	return 0;
}

/*===================================
 * Pontos de cash
 *-----------------------------------*/
ACMD_FUNC(cash)
{
	int value;
	nullpo_retr(-1, sd);

	if( !message || !*message || (value = atoi(message)) == 0 ) {
		clif_displaymessage(fd, "Por favor, entre com uma quantidade.");
		return -1;
	}

	if( !strcmpi(command+1,"cash") )
	{
		if( value > 0 )
			pc_getcash(sd, value, 0);
		else
			pc_paycash(sd, -value, 0);
	}
	else
	{ // @points
		if( value > 0 )
			pc_getcash(sd, 0, value);
		else
			pc_paycash(sd, -value, -value);
	}

	return 0;
}

// @clone/@slaveclone/@evilclone <nomejogador> [Valaris]
ACMD_FUNC(clone)
{
	int x=0,y=0,flag=0,master=0,i=0;
	struct map_session_data *pl_sd=NULL;

	if (!message || !*message) {
		clif_displaymessage(sd->fd,"Você precisa entrar com o nome ou ID de um personagem.");
		return 0;
	}

	if((pl_sd=map_nick2sd((char *)message)) == NULL && (pl_sd=map_charid2sd(atoi(message))) == NULL) {
		clif_displaymessage(fd, msg_txt(3));	// Personagem não encontrado.
		return 0;
	}

	if(pc_isGM(pl_sd) > pc_isGM(sd)) {
		clif_displaymessage(fd, msg_txt(126));	// Você não pode clonar um jogador de nível GM maior que o seu.
		return 0;
	}

	if (strcmpi(command+1, "clone") == 0) 
		flag = 1;
	else if (strcmpi(command+1, "slaveclone") == 0) {
	  	flag = 2;
		master = sd->bl.id;
		if (battle_config.atc_slave_clone_limit
			&& mob_countslave(&sd->bl) >= battle_config.atc_slave_clone_limit) {
			clif_displaymessage(fd, msg_txt(127));	// You've reached your slave clones limit.
			return 0;
		}
	}

	do {
		x = sd->bl.x + (rand() % 10 - 5);
		y = sd->bl.y + (rand() % 10 - 5);
	} while (map_getcell(sd->bl.m,x,y,CELL_CHKNOPASS) && i++ < 10);

	if (i >= 10) {
		x = sd->bl.x;
		y = sd->bl.y;
	}

	if((x = mob_clone_spawn(pl_sd, sd->bl.m, x, y, "", master, 0, flag?1:0, 0)) > 0) {
		clif_displaymessage(fd, msg_txt(128+flag*2));	// Clone do mal criado.. Clone criado. Clone escravo criado.
		return 0;
	}
	clif_displaymessage(fd, msg_txt(129+flag*2));	// Não é possível criar um clone do mal. Não é possível criar um clone. Não é possível criar um clone escravo.
	return 0;
}

/*===================================
 * Chat principal [LuzZza]
 * uso: @main <on|off|mensagem>
 *-----------------------------------*/
ACMD_FUNC(main)
{
	if( message[0] ) {

		if(strcmpi(message, "on") == 0) {
			if(!sd->state.mainchat) {
				sd->state.mainchat = 1;
				clif_displaymessage(fd, msg_txt(380)); // O chat principal foi ativado.
			} else {
				clif_displaymessage(fd, msg_txt(381)); // O chat principal já está ativado.
			}
		} else if(strcmpi(message, "off") == 0) {
			if(sd->state.mainchat) {
				sd->state.mainchat = 0;
				clif_displaymessage(fd, msg_txt(382)); // O chat principal foi desativado.
			} else {
				clif_displaymessage(fd, msg_txt(383)); // O chat principal já está desativado.
			}
		} else {
			if(!sd->state.mainchat) {
				sd->state.mainchat = 1;
				clif_displaymessage(fd, msg_txt(380)); // O chat principal foi ativado.
			}
			if (sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCHAT) {
				clif_displaymessage(fd, msg_txt(387));  // Você não pode usar o chat principal enquanto está silenciado.
				return -1;
			}
			sprintf(atcmd_output, msg_txt(386), sd->status.name, message); //%s principal: %s
			// Eu uso a cor 0xFE000000 para sinalizar que a mensagem está no
			// chat principal. 0xFE000000 é uma cor inválida, assim como
			// 0xFF000000 para mensagens de GM simples. [LuzZza]
			intif_broadcast2(atcmd_output, strlen(atcmd_output) + 1, 0xFE000000, 0, 0, 0, 0);

			// Tipo de login do char 'M' / Main Chat (Chat principal)
			if( log_config.chat&1 || (log_config.chat&32 && !((agit_flag || agit2_flag) && log_config.chat&64)) )
				log_chat("M", 0, sd->status.char_id, sd->status.account_id, mapindex_id2name(sd->mapindex), sd->bl.x, sd->bl.y, NULL, message);
		}
		
	} else {
	
		if(sd->state.mainchat) 
			clif_displaymessage(fd, msg_txt(384)); // O chat principal está atualmente ativado. Uso: @main <on|off>, @main <mensagem>.
		else
			clif_displaymessage(fd, msg_txt(385)); // O chat principal está atualmente desativado. Uo: @main <on|off>, @main <mensagem>.
	}
	return 0;
}

/*=====================================
 * Autorejecting Invites/Deals [LuzZza]
 * uso: @noask
 *-------------------------------------*/
ACMD_FUNC(noask)
{
	if(sd->state.noask) {
		clif_displaymessage(fd, msg_txt(391)); // Rejeição automática está desativada.
		sd->state.noask = 0;
	} else {
		clif_displaymessage(fd, msg_txt(390)); // Rejeição automática está ativada.
		sd->state.noask = 1;
	}
	
	return 0;
}

/*=====================================
 * Manda uma mensagem @request para todos os GMs de lowest_gm_level.
 * uso: @request <petição>
 *-------------------------------------*/
ACMD_FUNC(request)
{
	if (!message || !*message) {
		clif_displaymessage(sd->fd,msg_txt(277));	// uso: @request <mensagem para os GMs online>.
		return -1;
	}

	sprintf(atcmd_output, msg_txt(278), message);	// (@request): %s
	intif_wis_message_to_gm(sd->status.name, battle_config.lowest_gm_level, atcmd_output);
	clif_disp_onlyself(sd, atcmd_output, strlen(atcmd_output));
	clif_displaymessage(sd->fd,msg_txt(279));	// @request enviada.
	return 0;
}

/*==========================================
 * Feel (SG save map) Reset [HiddenDragon]
 *------------------------------------------*/
ACMD_FUNC(feelreset)
{
	pc_resetfeel(sd);
	clif_displaymessage(fd, "'Feeling' maps resetados.");

	return 0;
}

/*==========================================
 * SISTEMA DE LEILÃO
 *------------------------------------------*/
ACMD_FUNC(auction)
{
	nullpo_ret(sd);

#ifndef TXT_ONLY
	clif_Auction_openwindow(sd);
#endif

	return 0;
}

/*==========================================
 * Protenção contra KS
 *------------------------------------------*/
ACMD_FUNC(ksprotection)
{
	nullpo_retr(-1,sd);

	if( sd->state.noks ) {
		sd->state.noks = 0;
		sprintf(atcmd_output, "[ Proteção contra K.S desativada. ]");
	}
	else
	{
		if( !message || !*message || !strcmpi(message, "party") )
		{ // Default is Party
			sd->state.noks = 2;
			sprintf(atcmd_output, "[ Proteção de KS ativadas- Opção: Party ]");
		}
		else if( !strcmpi(message, "self") )
		{
			sd->state.noks = 1;
			sprintf(atcmd_output, "[ Proteção de KS ativada - Opção: Self ]");
		}
		else if( !strcmpi(message, "guild") )
		{
			sd->state.noks = 3;
			sprintf(atcmd_output, "[ Proteção de KS ativada - Opção: Guild ]");
		}
		else
			sprintf(atcmd_output, "uso: @noks <self|party|guild>");
	}

	clif_displaymessage(fd, atcmd_output);
	return 0;
}
/*==========================================
 * Proteção de KS no mapa.
 *------------------------------------------*/
ACMD_FUNC(allowks)
{
	nullpo_retr(-1,sd);

	if( map[sd->bl.m].flag.allowks ) {
		map[sd->bl.m].flag.allowks = 0;
		sprintf(atcmd_output, "[ Proteção de KS no mapa ativada. ]");
	} else {
		map[sd->bl.m].flag.allowks = 1;
		sprintf(atcmd_output, "[ Proteção de KS no mapa desativada. ]");
	}

	clif_displaymessage(fd, atcmd_output);
	return 0;
}

ACMD_FUNC(resetstat)
{
	nullpo_retr(-1, sd);
	
	pc_resetstate(sd);
	sprintf(atcmd_output, msg_txt(207), sd->status.name);
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

ACMD_FUNC(resetskill)
{
	nullpo_retr(-1,sd);
	
	pc_resetskill(sd,1);
	sprintf(atcmd_output, msg_txt(206), sd->status.name);
	clif_displaymessage(fd, atcmd_output);
	return 0;
}

/*==========================================
 * #storagelist: Mostra a lista de itens do armazém de um jogador.
 * #cartlist: Mostra o conteúdo do carrinho de um jogador.
 * #itemlist: Mostra o conteúdo do inventário de um jogador.
 *------------------------------------------*/
ACMD_FUNC(itemlist)
{
	int i, j, count, counter;
	const char* location;
	const struct item* items;
	int size;
	StringBuf buf;

	nullpo_retr(-1, sd);

	if( strcmp(command+1, "storagelist") == 0 )
	{
		location = "storage";
		items = sd->status.storage.items;
		size = MAX_STORAGE;
	}
	else
	if( strcmp(command+1, "cartlist") == 0 )
	{
		location = "cart";
		items = sd->status.cart;
		size = MAX_CART;
	}
	else
	if( strcmp(command+1, "itemlist") == 0 )
	{
		location = "inventory";
		items = sd->status.inventory;
		size = MAX_INVENTORY;
	}
	else
		return 1;

	StringBuf_Init(&buf);

	count = 0; 
	counter = 0;
	for( i = 0; i < size; ++i )
	{
		const struct item* it = &items[i];
		struct item_data* itd;

		if( it->nameid == 0 || (itd = itemdb_exists(it->nameid)) == NULL )
			continue;

		counter += it->amount;
		count++;

		if( count == 1 )
		{
			StringBuf_Printf(&buf, "------ %s lista de itens de '%s' ------", location, sd->status.name);
			clif_displaymessage(fd, StringBuf_Value(&buf));
			StringBuf_Clear(&buf);
		}

		if( it->refine )
			StringBuf_Printf(&buf, "%d %s %+d (%s, id: %d)", it->amount, itd->jname, it->refine, itd->name, it->nameid);
		else
			StringBuf_Printf(&buf, "%d %s (%s, id: %d)", it->amount, itd->jname, itd->name, it->nameid);

		if( it->equip )
		{
			char equipstr[CHAT_SIZE_MAX];
			strcpy(equipstr, " | equipped: ");
			if( it->equip & EQP_GARMENT )
				strcat(equipstr, "garment, ");
			if( it->equip & EQP_ACC_L )
				strcat(equipstr, "left accessory, ");
			if( it->equip & EQP_ARMOR )
				strcat(equipstr, "body/armor, ");
			if( (it->equip & EQP_ARMS) == EQP_HAND_R )
				strcat(equipstr, "right hand, ");
			if( (it->equip & EQP_ARMS) == EQP_HAND_L )
				strcat(equipstr, "left hand, ");
			if( (it->equip & EQP_ARMS) == EQP_ARMS )
				strcat(equipstr, "both hands, ");
			if( it->equip & EQP_SHOES )
				strcat(equipstr, "feet, ");
			if( it->equip & EQP_ACC_R )
				strcat(equipstr, "right accessory, ");
			if( (it->equip & EQP_HELM) == EQP_HEAD_LOW )
				strcat(equipstr, "lower head, ");
			if( (it->equip & EQP_HELM) == EQP_HEAD_TOP )
				strcat(equipstr, "top head, ");
			if( (it->equip & EQP_HELM) == (EQP_HEAD_LOW|EQP_HEAD_TOP) )
				strcat(equipstr, "lower/top head, ");
			if( (it->equip & EQP_HELM) == EQP_HEAD_MID )
				strcat(equipstr, "mid head, ");
			if( (it->equip & EQP_HELM) == (EQP_HEAD_LOW|EQP_HEAD_MID) )
				strcat(equipstr, "lower/mid head, ");
			if( (it->equip & EQP_HELM) == EQP_HELM )
				strcat(equipstr, "lower/mid/top head. ");
			// remove final ', '
			equipstr[strlen(equipstr) - 2] = '\0';
			StringBuf_AppendStr(&buf, equipstr);
		}

		clif_displaymessage(fd, StringBuf_Value(&buf));
		StringBuf_Clear(&buf);

		if( it->card[0] == CARD0_PET )
		{// pet egg
			if (it->card[3])
				StringBuf_Printf(&buf, " -> (pet egg, pet id: %u, named)", (unsigned int)MakeDWord(it->card[1], it->card[2]));
			else
				StringBuf_Printf(&buf, " -> (pet egg, pet id: %u, unnamed)", (unsigned int)MakeDWord(it->card[1], it->card[2]));
		}
		else
		if(it->card[0] == CARD0_FORGE)
		{
			StringBuf_Printf(&buf, " -> (item forjado, ID do criador: %u, poeira estelar %d, elemento %d)", (unsigned int)MakeDWord(it->card[2], it->card[3]), it->card[1]>>8, it->card[1]&0x0f);
		}
		else
		if(it->card[0] == CARD0_CREATE)
		{
			StringBuf_Printf(&buf, " -> (item criado, ID do criador: %u)", (unsigned int)MakeDWord(it->card[2], it->card[3]));
		}
		else
		{
			int counter2 = 0;

			for( j = 0; j < itd->slot; ++j )
			{
				struct item_data* card;

				if( it->card[j] == 0 || (card = itemdb_exists(it->card[j])) == NULL )
					continue;

				counter2++;

				if( counter2 == 1 )
					StringBuf_AppendStr(&buf, " -> (card(s): ");

				if( counter2 != 1 )
					StringBuf_AppendStr(&buf, ", ");

				StringBuf_Printf(&buf, "#%d %s (id: %d)", counter2, card->jname, card->nameid);
			}

			if( counter2 > 0 )
				StringBuf_AppendStr(&buf, ")");
		}

		if( StringBuf_Length(&buf) > 0 )
			clif_displaymessage(fd, StringBuf_Value(&buf));

		StringBuf_Clear(&buf);
	}

	if( count == 0 )
		StringBuf_Printf(&buf, "Nenhum item encontrado neste jogador %s.", location);
	else
		StringBuf_Printf(&buf, "%d item(s) encontrado(s) em %d %s slots.", counter, count, location);

	clif_displaymessage(fd, StringBuf_Value(&buf));

	StringBuf_Destroy(&buf);

	return 0;
}

ACMD_FUNC(stats)
{
	char job_jobname[100];
	char output[CHAT_SIZE_MAX];
	int i;
	struct {
		const char* format;
		int value;
	} output_table[] = {
		{ "Base Level - %d", 0 },
		{ NULL, 0 },
		{ "Hp - %d", 0 },
		{ "MaxHp - %d", 0 },
		{ "Sp - %d", 0 },
		{ "MaxSp - %d", 0 },
		{ "Str - %3d", 0 },
		{ "Agi - %3d", 0 },
		{ "Vit - %3d", 0 },
		{ "Int - %3d", 0 },
		{ "Dex - %3d", 0 },
		{ "Luk - %3d", 0 },
		{ "Zeny - %d", 0 },
		{ "Free SK Points - %d", 0 },
		{ "Used SK Points - %d", 0 },
		{ "JobChangeLvl - %d", 0 },
		{ "JobChangeLvl2 - %d", 0 },
		{ NULL, 0 }
	};

	memset(job_jobname, '\0', sizeof(job_jobname));
	memset(output, '\0', sizeof(output));

	output_table[0].value = sd->status.base_level;
	output_table[1].format = job_jobname;
	output_table[1].value = sd->status.job_level;
	output_table[2].value = sd->status.hp;
	output_table[3].value = sd->status.max_hp;
	output_table[4].value = sd->status.sp;
	output_table[5].value = sd->status.max_sp;
	output_table[6].value = sd->status.str;
	output_table[7].value = sd->status.agi;
	output_table[8].value = sd->status.vit;
	output_table[9].value = sd->status.int_;
	output_table[10].value = sd->status.dex;
	output_table[11].value = sd->status.luk;
	output_table[12].value = sd->status.zeny;
	output_table[13].value = sd->status.skill_point;
	output_table[14].value = pc_calc_skillpoint(sd);
	output_table[15].value = sd->change_level[0];
	output_table[16].value = sd->change_level[1];

	sprintf(job_jobname, "Job - %s %s", job_name(sd->status.class_), "(level %d)");
	sprintf(output, msg_txt(53), sd->status.name); // '%s' status:

	clif_displaymessage(fd, output);
	
	for (i = 0; output_table[i].format != NULL; i++) {
		sprintf(output, output_table[i].format, output_table[i].value);
		clif_displaymessage(fd, output);
	}

	return 0;
}

ACMD_FUNC(delitem)
{
	char item_name[100];
	int nameid, amount = 0, total, idx;
	struct item_data* id;

	nullpo_retr(-1, sd);

	if( !message || !*message || ( sscanf(message, "\"%99[^\"]\" %d", item_name, &amount) < 2 && sscanf(message, "%99s %d", item_name, &amount) < 2 ) || amount < 1 )
	{
		clif_displaymessage(fd, "Por favor, entre com o nome/ID de um item, uma quantidade de o nome de um jogador (uso: #delitem <jogador> <nome/ID do item> <quantidade>).");
		return -1;
	}

	if( ( id = itemdb_searchname(item_name) ) != NULL || ( id = itemdb_exists(atoi(item_name)) ) != NULL )
	{
		nameid = id->nameid;
	}
	else
	{
		clif_displaymessage(fd, msg_txt(19)); // Nome ou ID do item inválido.
		return -1;
	}

	total = amount;

	// deleta itens
	while( amount && ( idx = pc_search_inventory(sd, nameid) ) != -1 )
	{
		int delamount = ( amount < sd->status.inventory[idx].amount ) ? amount : sd->status.inventory[idx].amount;

		if( sd->inventory_data[idx]->type == IT_PETEGG && sd->status.inventory[idx].card[0] == CARD0_PET )
		{// deleta pet
			intif_delete_petdata(MakeDWord(sd->status.inventory[idx].card[1], sd->status.inventory[idx].card[2]));
		}

		//Logs (A)dmins items [Lupus]
		if( log_config.enable_logs&0x400 )
		{
			log_pick_pc(sd, "A", nameid, -delamount, &sd->status.inventory[idx]);
		}

		pc_delitem(sd, idx, delamount, 0, 0);

		amount-= delamount;
	}

	// notifica alvo
	sprintf(atcmd_output, msg_txt(113), total-amount); // %d item(ns) removido(s) por um GM.
	clif_displaymessage(sd->fd, atcmd_output);

	// notify source
	if( amount == total )
	{
		clif_displaymessage(fd, msg_txt(116)); // O personagem não tem item algum.
	}
	else if( amount )
	{
		sprintf(atcmd_output, msg_txt(115), total-amount, total-amount, total); // %d item(ns) removido(s). O jogador só tem %d em %d itens.
		clif_displaymessage(fd, atcmd_output);
	}
	else
	{
		sprintf(atcmd_output, msg_txt(114), total); // %d item(ns) removido(s) do jogador.
		clif_displaymessage(fd, atcmd_output);
	}

	return 0;
}

/*==========================================
 * Fontes custom
 *------------------------------------------*/
ACMD_FUNC(font)
{
	int font_id;
	nullpo_retr(-1,sd);

	font_id = atoi(message);
	if( font_id == 0 )
	{
		if( sd->user_font )
		{
			sd->user_font = 0;
			clif_displaymessage(fd, "Voltando para a fonte normal.");
			clif_font(sd);
		}
		else
		{
			clif_displaymessage(fd, "Use @font <1..9> para mudar a fonte das mensagens.");
			clif_displaymessage(fd, "Use 0 ou nenhum parâmetro para voltar à fonte normal.");
		}
	}
	else if( font_id < 0 || font_id > 9 )
		clif_displaymessage(fd, "Fonte inválida. Use um valor de 0 à 9.");
	else if( font_id != sd->user_font )
	{
		sd->user_font = font_id;
		clif_font(sd);
		clif_displaymessage(fd, "Fonte alterada.");
	}
	else
		clif_displaymessage(fd, "Você já está usando esta fonte.");

	return 0;
}


/*==========================================
 * atcommand_info[] definição da estrutura
 *------------------------------------------*/

AtCommandInfo atcommand_info[] = {
	{ "rura",              40,40,     atcommand_mapmove },
	{ "warp",              40,40,     atcommand_mapmove },
	{ "mapmove",           40,40,     atcommand_mapmove }, // + /mm
	{ "where",              1,1,      atcommand_where },
	{ "jumpto",            20,20,     atcommand_jumpto }, // + /shift
	{ "warpto",            20,20,     atcommand_jumpto },
	{ "goto",              20,20,     atcommand_jumpto },
	{ "jump",              40,40,     atcommand_jump },
	{ "who",               20,20,     atcommand_who },
	{ "whois",             20,20,     atcommand_who },
	{ "who2",              20,20,     atcommand_who2 },
	{ "who3",              20,20,     atcommand_who3 },
	{ "whomap",            20,20,     atcommand_whomap },
	{ "whomap2",           20,20,     atcommand_whomap2 },
	{ "whomap3",           20,20,     atcommand_whomap3 },
	{ "whogm",             20,20,     atcommand_whogm },
	{ "save",              40,40,     atcommand_save },
	{ "return",            40,40,     atcommand_load },
	{ "load",              40,40,     atcommand_load },
	{ "speed",             40,40,     atcommand_speed },
	{ "storage",            1,1,      atcommand_storage },
	{ "gstorage",          50,50,     atcommand_guildstorage },
	{ "option",            40,40,     atcommand_option },
	{ "hide",              40,40,     atcommand_hide }, // + /hide
	{ "jobchange",         40,40,     atcommand_jobchange },
	{ "job",               40,40,     atcommand_jobchange },
	{ "die",                1,1,      atcommand_die },
	{ "kill",              60,60,     atcommand_kill },
	{ "alive",             60,60,     atcommand_alive },
	{ "kami",              40,40,     atcommand_kami },
	{ "kamib",             40,40,     atcommand_kami },
	{ "kamic",             40,40,     atcommand_kami },
	{ "heal",              40,60,     atcommand_heal },
	{ "item",              60,60,     atcommand_item },
	{ "item2",             60,60,     atcommand_item2 },
	{ "costumeitem",             60,60,     atcommand_costumeitem },
	{ "itemreset",         40,40,     atcommand_itemreset },
	{ "blvl",              60,60,     atcommand_baselevelup },
	{ "lvup",              60,60,     atcommand_baselevelup },
	{ "blevel",            60,60,     atcommand_baselevelup },
	{ "baselvl",           60,60,     atcommand_baselevelup },
	{ "baselvup",          60,60,     atcommand_baselevelup },
	{ "baselevel",         60,60,     atcommand_baselevelup },
	{ "baselvlup",         60,60,     atcommand_baselevelup },
	{ "jlvl",              60,60,     atcommand_joblevelup },
	{ "jlevel",            60,60,     atcommand_joblevelup },
	{ "joblvl",            60,60,     atcommand_joblevelup },
	{ "joblevel",          60,60,     atcommand_joblevelup },
	{ "joblvup",           60,60,     atcommand_joblevelup },
	{ "joblvlup",          60,60,     atcommand_joblevelup },
	{ "h",                 20,20,     atcommand_help },
	{ "help",              20,20,     atcommand_help },
	{ "h2",                20,20,     atcommand_help2 },
	{ "help2",             20,20,     atcommand_help2 },
	{ "pvpoff",            40,40,     atcommand_pvpoff },
	{ "pvpon",             40,40,     atcommand_pvpon },
	{ "gvgoff",            40,40,     atcommand_gvgoff },
	{ "gpvpoff",           40,40,     atcommand_gvgoff },
	{ "gvgon",             40,40,     atcommand_gvgon },
	{ "gpvpon",            40,40,     atcommand_gvgon },
	{ "model",             20,20,     atcommand_model },
	{ "go",                10,10,     atcommand_go },
	{ "monster",           50,50,     atcommand_monster },
	{ "spawn",             50,50,     atcommand_monster },
	{ "monstersmall",      50,50,     atcommand_monstersmall },
	{ "monsterbig",        50,50,     atcommand_monsterbig },
	{ "killmonster",       60,60,     atcommand_killmonster },
	{ "killmonster2",      40,40,     atcommand_killmonster2 },
	{ "refine",            60,60,     atcommand_refine },
	{ "produce",           60,60,     atcommand_produce },
	{ "memo",              40,40,     atcommand_memo },
	{ "gat",               99,99,     atcommand_gat },
	{ "displaystatus",     99,99,     atcommand_displaystatus },
	{ "stpoint",           60,60,     atcommand_statuspoint },
	{ "skpoint",           60,60,     atcommand_skillpoint },
	{ "zeny",              60,60,     atcommand_zeny },
	{ "str",               60,60,     atcommand_param },
	{ "agi",               60,60,     atcommand_param },
	{ "vit",               60,60,     atcommand_param },
	{ "int",               60,60,     atcommand_param },
	{ "dex",               60,60,     atcommand_param },
	{ "luk",               60,60,     atcommand_param },
	{ "glvl",              60,60,     atcommand_guildlevelup },
	{ "glevel",            60,60,     atcommand_guildlevelup },
	{ "guildlvl",          60,60,     atcommand_guildlevelup },
	{ "guildlvup",         60,60,     atcommand_guildlevelup },
	{ "guildlevel",        60,60,     atcommand_guildlevelup },
	{ "guildlvlup",        60,60,     atcommand_guildlevelup },
	{ "makeegg",           60,60,     atcommand_makeegg },
	{ "hatch",             60,60,     atcommand_hatch },
	{ "petfriendly",       40,40,     atcommand_petfriendly },
	{ "pethungry",         40,40,     atcommand_pethungry },
	{ "petrename",          1,1,      atcommand_petrename },
	{ "recall",            60,60,     atcommand_recall }, // + /recall
	{ "night",             80,80,     atcommand_night },
	{ "day",               80,80,     atcommand_day },
	{ "doom",              80,80,     atcommand_doom },
	{ "doommap",           80,80,     atcommand_doommap },
	{ "raise",             80,80,     atcommand_raise },
	{ "raisemap",          80,80,     atcommand_raisemap },
	{ "kick",              20,20,     atcommand_kick }, // + right click menu for GM "(name) force to quit"
	{ "kickall",           99,99,     atcommand_kickall },
	{ "allskill",          60,60,     atcommand_allskill },
	{ "allskills",         60,60,     atcommand_allskill },
	{ "skillall",          60,60,     atcommand_allskill },
	{ "skillsall",         60,60,     atcommand_allskill },
	{ "questskill",        40,40,     atcommand_questskill },
	{ "lostskill",         40,40,     atcommand_lostskill },
	{ "spiritball",        40,40,     atcommand_spiritball },
	{ "party",              1,1,      atcommand_party },
	{ "guild",             50,50,     atcommand_guild },
	{ "agitstart",         60,60,     atcommand_agitstart },
	{ "agitend",           60,60,     atcommand_agitend },
	{ "mapexit",           99,99,     atcommand_mapexit },
	{ "idsearch",          60,60,     atcommand_idsearch },
	{ "broadcast",         40,40,     atcommand_broadcast }, // + /b and /nb
	{ "localbroadcast",    40,40,     atcommand_localbroadcast }, // + /lb and /nlb
	{ "recallall",         80,80,     atcommand_recallall },
	{ "reloaditemdb",      99,99,     atcommand_reloaditemdb },
	{ "reloadmobdb",       99,99,     atcommand_reloadmobdb },
	{ "reloadskilldb",     99,99,     atcommand_reloadskilldb },
	{ "reloadscript",      99,99,     atcommand_reloadscript },
	{ "reloadatcommand",   99,99,     atcommand_reloadatcommand },
	{ "reloadbattleconf",  99,99,     atcommand_reloadbattleconf },
	{ "reloadstatusdb",    99,99,     atcommand_reloadstatusdb },
	{ "reloadpcdb",        99,99,     atcommand_reloadpcdb },
	{ "reloadmotd",        99,99,     atcommand_reloadmotd },
	{ "mapinfo",           99,99,     atcommand_mapinfo },
	{ "dye",               40,40,     atcommand_dye },
	{ "ccolor",            40,40,     atcommand_dye },
	{ "hairstyle",         40,40,     atcommand_hair_style },
	{ "hstyle",            40,40,     atcommand_hair_style },
	{ "haircolor",         40,40,     atcommand_hair_color },
	{ "hcolor",            40,40,     atcommand_hair_color },
	{ "statall",           60,60,     atcommand_stat_all },
	{ "statsall",          60,60,     atcommand_stat_all },
	{ "allstats",          60,60,     atcommand_stat_all },
	{ "allstat",           60,60,     atcommand_stat_all },
	{ "block",             60,60,     atcommand_char_block },
	{ "charblock",         60,60,     atcommand_char_block },
	{ "ban",               60,60,     atcommand_char_ban },
	{ "banish",            60,60,     atcommand_char_ban },
	{ "charban",           60,60,     atcommand_char_ban },
	{ "charbanish",        60,60,     atcommand_char_ban },
	{ "unblock",           60,60,     atcommand_char_unblock },
	{ "charunblock",       60,60,     atcommand_char_unblock },
	{ "unban",             60,60,     atcommand_char_unban },
	{ "unbanish",          60,60,     atcommand_char_unban },
	{ "charunban",         60,60,     atcommand_char_unban },
	{ "charunbanish",      60,60,     atcommand_char_unban },
	{ "mount",             20,20,     atcommand_mount },
	{ "guildspy",          60,60,     atcommand_guildspy },
	{ "partyspy",          60,60,     atcommand_partyspy },
	{ "repairall",         60,60,     atcommand_repairall },
	{ "guildrecall",       60,60,     atcommand_guildrecall },
	{ "partyrecall",       60,60,     atcommand_partyrecall },
	{ "nuke",              60,60,     atcommand_nuke },
	{ "shownpc",           80,80,     atcommand_shownpc },
	{ "hidenpc",           80,80,     atcommand_hidenpc },
	{ "loadnpc",           80,80,     atcommand_loadnpc },
	{ "unloadnpc",         80,80,     atcommand_unloadnpc },
	{ "time",               1,1,      atcommand_servertime },
	{ "date",               1,1,      atcommand_servertime },
	{ "serverdate",         1,1,      atcommand_servertime },
	{ "servertime",         1,1,      atcommand_servertime },
	{ "jail",              60,60,     atcommand_jail },
	{ "unjail",            60,60,     atcommand_unjail },
	{ "discharge",         60,60,     atcommand_unjail },
	{ "jailfor",           60,60,     atcommand_jailfor },
	{ "jailtime",           1,1,      atcommand_jailtime },
	{ "disguise",          20,20,     atcommand_disguise },
	{ "undisguise",        20,20,     atcommand_undisguise },
	{ "email",              1,1,      atcommand_email },
	{ "effect",            40,40,     atcommand_effect },
	{ "follow",            20,20,     atcommand_follow },
	{ "addwarp",           60,60,     atcommand_addwarp },
	{ "skillon",           80,80,     atcommand_skillon },
	{ "skilloff",          80,80,     atcommand_skilloff },
	{ "killer",            60,60,     atcommand_killer },
	{ "npcmove",           80,80,     atcommand_npcmove },
	{ "killable",          40,40,     atcommand_killable },
	{ "dropall",           40,40,     atcommand_dropall },
	{ "storeall",          40,40,     atcommand_storeall },
	{ "skillid",           40,40,     atcommand_skillid },
	{ "useskill",          40,40,     atcommand_useskill },
	{ "displayskill",      99,99,     atcommand_displayskill },
	{ "snow",              99,99,     atcommand_snow },
	{ "sakura",            99,99,     atcommand_sakura },
	{ "clouds",            99,99,     atcommand_clouds },
	{ "clouds2",           99,99,     atcommand_clouds2 },
	{ "fog",               99,99,     atcommand_fog },
	{ "fireworks",         99,99,     atcommand_fireworks },
	{ "leaves",            99,99,     atcommand_leaves },
	{ "summon",            60,60,     atcommand_summon },
	{ "adjgmlvl",          99,99,     atcommand_adjgmlvl },
	{ "adjcmdlvl",         99,99,     atcommand_adjcmdlvl },
	{ "trade",             60,60,     atcommand_trade },
	{ "send",              99,99,     atcommand_send },
	{ "setbattleflag",     99,99,     atcommand_setbattleflag },
	{ "unmute",            80,80,     atcommand_unmute },
	{ "clearweather",      99,99,     atcommand_clearweather },
	{ "uptime",             1,1,      atcommand_uptime },
	{ "changesex",         60,60,     atcommand_changesex },
	{ "mute",              80,80,     atcommand_mute },
	{ "refresh",            1,1,      atcommand_refresh },
	{ "identify",          40,40,     atcommand_identify },
	{ "gmotd",             20,20,     atcommand_gmotd },
	{ "misceffect",        50,50,     atcommand_misceffect },
	{ "mobsearch",         10,10,     atcommand_mobsearch },
	{ "cleanmap",          40,40,     atcommand_cleanmap },
	{ "npctalk",           20,20,     atcommand_npctalk },
	{ "npctalkc",          20,20,     atcommand_npctalk },
	{ "pettalk",           10,10,     atcommand_pettalk },
	{ "users",             40,40,     atcommand_users },
	{ "reset",             40,40,     atcommand_reset },
	{ "skilltree",         40,40,     atcommand_skilltree },
	{ "marry",             40,40,     atcommand_marry },
	{ "divorce",           40,40,     atcommand_divorce },
	{ "sound",             40,40,     atcommand_sound },
	{ "undisguiseall",     99,99,     atcommand_undisguiseall },
	{ "disguiseall",       99,99,     atcommand_disguiseall },
	{ "changelook",        60,60,     atcommand_changelook },
	{ "autoloot",          10,10,     atcommand_autoloot },
	{ "alootid",           10,10,     atcommand_autolootitem },
	{ "mobinfo",            1,1,      atcommand_mobinfo },
	{ "monsterinfo",        1,1,      atcommand_mobinfo },
	{ "mi",                 1,1,      atcommand_mobinfo },
	{ "exp",                1,1,      atcommand_exp },
	{ "adopt",             40,40,     atcommand_adopt },
	{ "version",            1,1,      atcommand_version },
	{ "mutearea",          99,99,     atcommand_mutearea },
	{ "stfu",              99,99,     atcommand_mutearea },
	{ "rates",              1,1,      atcommand_rates },
	{ "iteminfo",           1,1,      atcommand_iteminfo },
	{ "ii",                 1,1,      atcommand_iteminfo },
	{ "whodrops",           1,1,      atcommand_whodrops },
	{ "whereis",           10,10,     atcommand_whereis },
	{ "mapflag",           99,99,     atcommand_mapflag },
	{ "me",                20,20,     atcommand_me },
	{ "monsterignore",     99,99,     atcommand_monsterignore },
	{ "battleignore",      99,99,     atcommand_monsterignore },
	{ "fakename",          20,20,     atcommand_fakename },
	{ "size",              20,20,     atcommand_size },
	{ "showexp",           10,10,     atcommand_showexp},
	{ "showzeny",          10,10,     atcommand_showzeny},
	{ "showdelay",          1,1,      atcommand_showdelay},
	{ "autotrade",         10,10,     atcommand_autotrade },
	{ "at",                10,10,     atcommand_autotrade },
	{ "changegm",          10,10,     atcommand_changegm },
	{ "changeleader",      10,10,     atcommand_changeleader },
	{ "partyoption",       10,10,     atcommand_partyoption},
	{ "invite",             1,1,      atcommand_invite },
	{ "duel",               1,1,      atcommand_duel },
	{ "leave",              1,1,      atcommand_leave },
	{ "accept",             1,1,      atcommand_accept },
	{ "reject",             1,1,      atcommand_reject },
	{ "main",               1,1,      atcommand_main },
	{ "clone",             50,50,     atcommand_clone },
	{ "slaveclone",        50,50,     atcommand_clone },
	{ "evilclone",         50,50,     atcommand_clone },
	{ "tonpc",             40,40,     atcommand_tonpc },
	{ "commands",           1,1,      atcommand_commands },
	{ "noask",              1,1,      atcommand_noask },
	{ "request",           20,20,     atcommand_request },
	{ "hlvl",              60,60,     atcommand_homlevel },
	{ "hlevel",            60,60,     atcommand_homlevel },
	{ "homlvl",            60,60,     atcommand_homlevel },
	{ "homlvup",           60,60,     atcommand_homlevel },
	{ "homlevel",          60,60,     atcommand_homlevel },
	{ "homevolve",         60,60,     atcommand_homevolution },
	{ "homevolution",      60,60,     atcommand_homevolution },
	{ "makehomun",         60,60,     atcommand_makehomun },
	{ "homfriendly",       60,60,     atcommand_homfriendly },
	{ "homhungry",         60,60,     atcommand_homhungry },
	{ "homtalk",           10,10,     atcommand_homtalk },
	{ "hominfo",            1,1,      atcommand_hominfo },
	{ "homstats",           1,1,      atcommand_homstats },
	{ "homshuffle",        60,60,     atcommand_homshuffle },
	{ "showmobs",          10,10,     atcommand_showmobs },
	{ "feelreset",         10,10,     atcommand_feelreset },
	{ "auction",            1,1,      atcommand_auction },
	{ "mail",               1,1,      atcommand_mail },
	{ "noks",               1,1,      atcommand_ksprotection },
	{ "allowks",           40,40,     atcommand_allowks },
	{ "cash",              60,60,     atcommand_cash },
	{ "points",            60,60,     atcommand_cash },
	{ "agitstart2",        60,60,     atcommand_agitstart2 },
	{ "agitend2",          60,60,     atcommand_agitend2 },
	{ "skreset",           60,60,     atcommand_resetskill },
	{ "streset",           60,60,     atcommand_resetstat },
	{ "storagelist",       40,40,     atcommand_itemlist },
	{ "cartlist",          40,40,     atcommand_itemlist },
	{ "itemlist",          40,40,     atcommand_itemlist },
	{ "stats",             40,40,     atcommand_stats },
	{ "delitem",           60,60,     atcommand_delitem },
	{ "charcommands",       1,1,      atcommand_commands },
	{ "font",               1,1,      atcommand_font },
};


/*==========================================
 * Comando de funções de pesquisa
 *------------------------------------------*/
static AtCommandInfo* get_atcommandinfo_byname(const char* name)
{
	int i;
	if( *name == atcommand_symbol || *name == charcommand_symbol ) name++;
	ARR_FIND( 0, ARRAYLENGTH(atcommand_info), i, strcmpi(atcommand_info[i].command, name) == 0 );
	return ( i < ARRAYLENGTH(atcommand_info) ) ? &atcommand_info[i] : NULL;
}

static AtCommandInfo* get_atcommandinfo_byfunc(const AtCommandFunc func)
{
	int i;
	ARR_FIND( 0, ARRAYLENGTH(atcommand_info), i, atcommand_info[i].func == func );
	return ( i < ARRAYLENGTH(atcommand_info) ) ? &atcommand_info[i] : NULL;
}


/*==========================================
 * Recupera o nível de GM requerido pelo comando
 *------------------------------------------*/
int get_atcommand_level(const AtCommandFunc func)
{
	AtCommandInfo* info = get_atcommandinfo_byfunc(func);
	return ( info != NULL ) ? info->level : 100; // 100: o comando não pode ser usado
}


/// Executa um at-command.
bool is_atcommand(const int fd, struct map_session_data* sd, const char* message, int type)
{
	char charname[NAME_LENGTH], params[100];
	char charname2[NAME_LENGTH], params2[100];
	char command[100];
	char output[CHAT_SIZE_MAX];
	int x, y, z;
	int lv = 0;
	
	//Mensagem reconstruída
	char atcmd_msg[CHAT_SIZE_MAX];
	
	TBL_PC * ssd = NULL; //sd para alvo.
	AtCommandInfo * info;

	nullpo_retr(false, sd);
	
	//não deve acontecer.
	if( !message || !*message )
		return false;
	
	//Bloqueira NOCHAT mas não mostra como uma mensagem normal
	if( sd->sc.data[SC_NOCHAT] && sd->sc.data[SC_NOCHAT]->val1&MANNER_NOCOMMAND )
		return true;
		
	// pula os langtypes 10/11
	if( message[0] == '|' && strlen(message) >= 4 && (message[3] == atcommand_symbol || message[3] == charcommand_symbol) )
		message += 3;
		
	//Deve mostrar uma mensagem normal.
	if ( *message != atcommand_symbol && *message != charcommand_symbol )
		return false;
	
	// tipo valor 0 = server invocado: bypass restrições
	// 1 = player invocado
	if( type )
	{
		//Comandos bloqueados pela mapflag nocommand.
		if( map[sd->bl.m].nocommand && pc_isGM(sd) < map[sd->bl.m].nocommand )
		{
			clif_displaymessage(fd, msg_txt(143));
			return false;
		}
		
		//Mostra uma mensagem normal para os não-GMs.
		if( battle_config.atc_gmonly != 0 && pc_isGM(sd) == 0 )
			return false;	
	}

	while (*message == charcommand_symbol)
	{	
		
		x = sscanf(message, "%99s \"%23[^\"]\" %99[^\n]", command, charname, params);
		y = sscanf(message, "%99s %23s %99[^\n]", command, charname2, params2);
		
		//z sempre tem o valor do scan que foi bem sucedido
		z = ( x > 1 ) ? x : y;
		
		if ( (ssd = map_nick2sd(charname)) == NULL  && ( (ssd = map_nick2sd(charname2)) == NULL ) )
		{
			sprintf(output, "%s falhou. Jogador não encontrado.", command);
			clif_displaymessage(fd, output);
			return true;
		}
		
		//#command + nome significa que foram usados alvos suficiente e nada mais depois
		if ( x > 2 ) {
			sprintf(atcmd_msg, "%s %s", command, params);
			break;
		}
		else if ( y > 2 ) {
			sprintf(atcmd_msg, "%s %s", command, params2);
			break;
		}
		//Independentemente do estilo que o #command é usado, se não é correto, sempre vai ter
		//este valor se não houver parâmetro. Manda isso só como #command
		else if ( z == 2 ) {
			sprintf(atcmd_msg, "%s", command);
			break;
		}
		
		sprintf(output, "Charcommand falhou. uso: #<command> <nome_char> <parâmetros>.");
		clif_displaymessage(fd, output);
		return true;
	}
	
	if (*message == atcommand_symbol) {
		//atcmd_msg é contruído diferentemente para charcommands
		sprintf(atcmd_msg, "%s", message);
	}
	
	//Limpando isso para ser usado de novo.
	memset(command, '\0', sizeof(command));
	memset(params, '\0', sizeof(params));
	
	//Checa se parâmetros existem neste comando.
	if( sscanf(atcmd_msg, "%99s %99[^\n]", command, params) < 2 )
		params[0] = '\0';
	
	//Pega a informação do comando e checa se o nível de GM requerido para usá-lo ou se o comando existe
	info = get_atcommandinfo_byname(command);
	if( info == NULL || info->func == NULL || ( type && ((*atcmd_msg == atcommand_symbol && pc_isGM(sd) < info->level) || (*atcmd_msg == charcommand_symbol && pc_isGM(sd) < info->level2)) ) )
	{
			sprintf(output, msg_txt(153), command); // %s é um comando inválido.
			clif_displaymessage(fd, output);
			return true;
	}
	
	if( strcmpi("adjgmlvl",command+1) && ssd ) { lv = ssd->gmlevel; ssd->gmlevel = sd->gmlevel; }
	if ( (info->func(fd, (*atcmd_msg == atcommand_symbol) ? sd : ssd, command, params) != 0) )
	{
		sprintf(output,msg_txt(154), command); // %s falhou.
		clif_displaymessage(fd, output);
	}
	if( strcmpi("adjgmlvl",command+1) && ssd ) ssd->gmlevel = lv;
	
	//Log atcommands
	if( log_config.gm && info->level >= log_config.gm && *atcmd_msg == atcommand_symbol )
		log_atcommand(sd, atcmd_msg);
		
	//Log Charcommands
	if( log_config.gm && info->level2 >= log_config.gm && *atcmd_msg == charcommand_symbol && ssd != NULL )
		log_atcommand(sd, message);
	
	return true;
}


/*==========================================
 *
 *------------------------------------------*/
int atcommand_config_read(const char* cfgName)
{
	char line[1024], w1[1024], w2[1024], w3[1024];
	AtCommandInfo* p;
	FILE* fp;
	
	if( (fp = fopen(cfgName, "r")) == NULL )
	{
		ShowError("Arquivo de configuração de AtCommand não encontrado: %s\n", cfgName);
		return 1;
	}
	
	while( fgets(line, sizeof(line), fp) )
	{
		if( line[0] == '/' && line[1] == '/' )
			continue;
		
		if( (sscanf(line, "%1023[^:]:%1023[^,],%1023s", w1, w2, w3)) != 3 && ( sscanf(line, "%1023[^:]:%1023s", w1, w2) != 2 
		&& strcmpi(w1, "import") != 0 ) && strcmpi(w1, "command_symbol") != 0 && strcmpi(w1, "char_symbol") != 0 )
			continue;

		p = get_atcommandinfo_byname(w1);
		if( p != NULL )
		{
			p->level = atoi(w2);
			p->level = cap_value(p->level, 0, 100);
			if( (sscanf(line, "%1023[^:]:%1023s", w1, w2) == 2) && (sscanf(line, "%1023[^:]:%1023[^,],%1023s", w1, w2, w3)) != 3 )
			{	
				ShowWarning("atcommand_conf: setting %s:%d is deprecated! Por favor see atcommand_athena.conf for the new setting format.\n",w1,atoi(w2));
				ShowWarning("atcommand_conf: defaulting %s charcommand level to %d.\n",w1,atoi(w2));
				p->level2 = atoi(w2);
			}
			else {
				p->level2 = atoi(w3);
			}
			p->level2 = cap_value(p->level2, 0, 100);
		}
		else
		if( strcmpi(w1, "import") == 0 )
			atcommand_config_read(w2);
		else
		if( strcmpi(w1, "command_symbol") == 0 &&
			w2[0] > 31   && // caracteres de controle
			w2[0] != '/' && // símbolos de comandos de GM padrões
			w2[0] != '%' && // símbolos de conversa entre uma party.
			w2[0] != '$' && // símbolo de conversa entre um clã.
			w2[0] != '#' ) // símbolo remoto.
			atcommand_symbol = w2[0];
		else 
		if( strcmpi(w1, "char_symbol") == 0 &&
			w2[0] > 31   &&
			w2[0] != '/' &&
			w2[0] != '%' &&
			w2[0] != '$' &&
			w2[0] != '@' )
			charcommand_symbol = w2[0];
		else
			ShowWarning("Configuração desconhecida '%s' no arquivo %s\n", w1, cfgName);
	}
	fclose(fp);

	return 0;
}

void do_init_atcommand()
{
	add_timer_func_list(atshowmobs_timer, "atshowmobs_timer");
	return;
}

void do_final_atcommand()
{
}


// comandos que precisam go _after_ a tabela de comandos

/*==========================================
 * @commands lista os comandos @ disponíveis.
 *------------------------------------------*/
ACMD_FUNC(commands)
{
	char line_buff[CHATBOX_SIZE];
	int i, gm_lvl = pc_isGM(sd), count = 0;
	char* cur = line_buff;

	memset(line_buff,' ',CHATBOX_SIZE);
	line_buff[CHATBOX_SIZE-1] = 0;

	clif_displaymessage(fd, msg_txt(273)); // "Comandos disponíveis:"

	for( i = 0; i < ARRAYLENGTH(atcommand_info); i++ )
	{
		unsigned int slen;

		if( gm_lvl < atcommand_info[i].level && stristr(command,"commands") )
			continue;
		if( gm_lvl < atcommand_info[i].level2 && stristr(command,"charcommands") )
			continue;

		slen = strlen(atcommand_info[i].command);

		// liberar o comando de buffer de texto não vai caber aí.
		if( slen + cur - line_buff >= CHATBOX_SIZE )
		{
			clif_displaymessage(fd,line_buff);
			cur = line_buff;
			memset(line_buff,' ',CHATBOX_SIZE);
			line_buff[CHATBOX_SIZE-1] = 0;
		}

		memcpy(cur,atcommand_info[i].command,slen);
		cur += slen+(10-slen%10);

		count++;
	}
	clif_displaymessage(fd,line_buff);

	sprintf(atcmd_output, msg_txt(274), count); // "%d comandos encontrados."
	clif_displaymessage(fd, atcmd_output);

	return 0;
}
