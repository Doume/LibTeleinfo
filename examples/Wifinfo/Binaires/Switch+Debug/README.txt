Le fichier Wifinfo.cpp.bin disponible dans ce dossier a été compilé
le 10/12/2019 (version 1.0.7)
Compilé sans l'option DEBUG (pas d'écriture sur le port Serial1) 
et sans la gestion d'un contact sec (option SENSOR désactivée)

Mais l'option SYSLOG a été activée, donc il est possible d'obtenir
  du debugging via un remote syslog, à condition de configurer
  syslog host et syslog port dans la configuration avancée

 Il s'agit donc d'une version 'standard' production

Il peut être flashé via port série (outil ESPTOOL), 
ou via l'interface Web de Wifinfo, à condition que vous ayez au préalable
flashé le SPIFFS permettant la prise en compte des fichiers .ino.bin ou .cpp.bin
 (version 1.0.5a minimum)
