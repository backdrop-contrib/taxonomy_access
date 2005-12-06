-- 
--Table structure for table `term_access`
-- 

CREATE TABLE term_access (
 tid integer  NOT NULL default '0',
 rid integer  NOT NULL default '0',
 grant_view smallint NOT NULL default '0',
 grant_update smallint NOT NULL default '0',
 grant_delete smallint NOT NULL default '0',
 grant_create smallint NOT NULL default '0',
 PRIMARY KEY  (tid,rid)
);

CREATE TABLE term_access_defaults (
 vid integer  NOT NULL default '0',
 rid integer  NOT NULL default '0',
 grant_view smallint NOT NULL default '0',
 grant_update smallint NOT NULL default '0',
 grant_delete smallint NOT NULL default '0',
 grant_create smallint NOT NULL default '0',
 PRIMARY KEY  (vid,rid)
); 