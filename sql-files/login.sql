/*
SQLyog Community v12.4.3 (64 bit)
MySQL - 5.1.49-community : Database - ro
*********************************************************************
*/

/*!40101 SET NAMES utf8 */;

/*!40101 SET SQL_MODE=''*/;

/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;
CREATE DATABASE /*!32312 IF NOT EXISTS*/`ro` /*!40100 DEFAULT CHARACTER SET latin1 */;

USE `ro`;

/*Table structure for table `class` */

DROP TABLE IF EXISTS `class`;

CREATE TABLE `class` (
  `id` int(11) NOT NULL,
  `port` varchar(19) DEFAULT NULL,
  `eng` varchar(15) DEFAULT NULL,
  `skills` smallint(6) DEFAULT NULL,
  `joblv` tinyint(4) DEFAULT NULL,
  `baseclass` int(11) DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

/*Table structure for table `donate` */

DROP TABLE IF EXISTS `donate`;

CREATE TABLE `donate` (
  `account_id` int(11) unsigned NOT NULL,
  `amount` float(10,2) NOT NULL DEFAULT '0.00',
  `claimed` float(10,2) DEFAULT NULL,
  PRIMARY KEY (`account_id`,`amount`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

/*Table structure for table `donate_item_db` */

DROP TABLE IF EXISTS `donate_item_db`;

CREATE TABLE `donate_item_db` (
  `id` int(11) NOT NULL,
  `name` varchar(30) DEFAULT NULL,
  `price` int(11) DEFAULT NULL,
  `normalprice` int(11) DEFAULT NULL,
  `tipo` tinyint(4) NOT NULL,
  `serv` tinyint(4) NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

/*Table structure for table `donate_log` */

DROP TABLE IF EXISTS `donate_log`;

CREATE TABLE `donate_log` (
  `dt` datetime DEFAULT NULL,
  `item` int(11) DEFAULT NULL,
  `qtde` int(11) DEFAULT NULL,
  `vlr` float(6,2) DEFAULT NULL,
  `account_id` int(11) DEFAULT NULL,
  `serv` tinyint(4) DEFAULT '0'
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

/*Table structure for table `donate_tipos` */

DROP TABLE IF EXISTS `donate_tipos`;

CREATE TABLE `donate_tipos` (
  `id_tipo` tinyint(4) DEFAULT NULL,
  `tipo` varchar(15) DEFAULT NULL,
  `npc` varchar(15) DEFAULT NULL
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

/*Table structure for table `global_reg_value` */

DROP TABLE IF EXISTS `global_reg_value`;

CREATE TABLE `global_reg_value` (
  `char_id` int(11) unsigned NOT NULL DEFAULT '0',
  `str` varchar(255) NOT NULL DEFAULT '',
  `value` varchar(255) NOT NULL DEFAULT '0',
  `type` int(11) NOT NULL DEFAULT '3',
  `account_id` int(11) unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`char_id`,`str`,`account_id`),
  KEY `account_id` (`account_id`),
  KEY `char_id` (`char_id`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

/*Table structure for table `ipbanlist` */

DROP TABLE IF EXISTS `ipbanlist`;

CREATE TABLE `ipbanlist` (
  `list` varchar(255) NOT NULL DEFAULT '',
  `btime` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `rtime` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `reason` varchar(255) NOT NULL DEFAULT '',
  KEY `list` (`list`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

/*Table structure for table `login` */

DROP TABLE IF EXISTS `login`;

CREATE TABLE `login` (
  `account_id` int(11) unsigned NOT NULL AUTO_INCREMENT,
  `userid` varchar(255) NOT NULL DEFAULT '',
  `user_pass` varchar(32) NOT NULL DEFAULT '',
  `lastlogin` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `created` datetime DEFAULT NULL,
  `sex` char(1) NOT NULL DEFAULT 'M',
  `logincount` mediumint(9) unsigned NOT NULL DEFAULT '0',
  `email` varchar(60) NOT NULL DEFAULT '',
  `level` tinyint(3) NOT NULL DEFAULT '0',
  `error_message` smallint(11) unsigned NOT NULL DEFAULT '0',
  `expiration_time` int(11) unsigned NOT NULL DEFAULT '0',
  `last_ip` varchar(100) NOT NULL DEFAULT '',
  `memo` smallint(11) unsigned NOT NULL DEFAULT '0',
  `unban_time` int(11) unsigned NOT NULL DEFAULT '0',
  `state` int(11) unsigned NOT NULL DEFAULT '0',
  `banco` int(11) DEFAULT '0',
  `forum_id` int(11) DEFAULT NULL,
  `banco5` int(11) DEFAULT NULL,
  `banco6` int(11) DEFAULT NULL,
  `errou_qtde` int(11) NOT NULL DEFAULT '0',
  `errou_ultima` datetime DEFAULT NULL,
  `faction_id` int(11) DEFAULT NULL,
  `birthdate` date NOT NULL DEFAULT '0000-00-00',
  PRIMARY KEY (`account_id`),
  KEY `name` (`userid`),
  KEY `idx_logins` (`last_ip`,`lastlogin`)
) ENGINE=MyISAM AUTO_INCREMENT=6026156 DEFAULT CHARSET=latin1;

/*Table structure for table `login_emails` */

DROP TABLE IF EXISTS `login_emails`;

CREATE TABLE `login_emails` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `tempo` datetime DEFAULT NULL,
  `account_id` int(11) DEFAULT NULL,
  `email_ant` varchar(50) DEFAULT NULL,
  `email_novo` varchar(50) DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM AUTO_INCREMENT=1403 DEFAULT CHARSET=latin1;

/*Table structure for table `login_senhas` */

DROP TABLE IF EXISTS `login_senhas`;

CREATE TABLE `login_senhas` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `tempo` datetime DEFAULT NULL,
  `account_id` int(11) DEFAULT NULL,
  `pass_ant` varchar(30) DEFAULT NULL,
  `pass_novo` varchar(30) DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM AUTO_INCREMENT=21779 DEFAULT CHARSET=latin1;

/*Table structure for table `login_tentativas` */

DROP TABLE IF EXISTS `login_tentativas`;

CREATE TABLE `login_tentativas` (
  `ip` varchar(15) NOT NULL,
  `ultima` datetime DEFAULT NULL,
  `qtde` int(11) DEFAULT NULL,
  PRIMARY KEY (`ip`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

/*Table structure for table `loginlog` */

DROP TABLE IF EXISTS `loginlog`;

CREATE TABLE `loginlog` (
  `time` datetime NOT NULL DEFAULT '0000-00-00 00:00:00',
  `ip` varchar(64) NOT NULL DEFAULT '',
  `user` varchar(34) NOT NULL DEFAULT '',
  `rcode` tinyint(4) DEFAULT '-1',
  `log` varchar(255) NOT NULL DEFAULT ''
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

/*Table structure for table `questions` */

DROP TABLE IF EXISTS `questions`;

CREATE TABLE `questions` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `Question` varchar(300) DEFAULT NULL,
  `Answers` varchar(100) DEFAULT NULL,
  `Correct` tinyint(4) DEFAULT NULL,
  PRIMARY KEY (`id`)
) ENGINE=MyISAM AUTO_INCREMENT=292 DEFAULT CHARSET=latin1;

/*Table structure for table `ragsrvinfo` */

DROP TABLE IF EXISTS `ragsrvinfo`;

CREATE TABLE `ragsrvinfo` (
  `index` int(11) NOT NULL DEFAULT '0',
  `name` varchar(255) NOT NULL DEFAULT '',
  `exp` int(11) unsigned NOT NULL DEFAULT '0',
  `jexp` int(11) unsigned NOT NULL DEFAULT '0',
  `drop` int(11) unsigned NOT NULL DEFAULT '0',
  `agit_status` tinyint(1) unsigned NOT NULL DEFAULT '0',
  KEY `name` (`name`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8;

/*Table structure for table `sstatus` */

DROP TABLE IF EXISTS `sstatus`;

CREATE TABLE `sstatus` (
  `index` tinyint(4) unsigned NOT NULL DEFAULT '0',
  `name` varchar(255) NOT NULL DEFAULT '',
  `user` int(11) unsigned NOT NULL DEFAULT '0'
) ENGINE=MyISAM DEFAULT CHARSET=latin1;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;
