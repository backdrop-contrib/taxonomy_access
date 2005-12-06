-- 
--PRIOR TO POSTGRESQL 8.0: making a BIT_OR aggregate function
-- 

CREATE AGGREGATE BIT_OR
(
  basetype = smallint,
  sfunc = int2or,
  stype = smallint
);

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

-- 
--Table structure for table `term_access_defaults`
-- 

CREATE TABLE term_access_defaults (
 vid integer  NOT NULL default '0',
 rid integer  NOT NULL default '0',
 grant_view smallint NOT NULL default '0',
 grant_update smallint NOT NULL default '0',
 grant_delete smallint NOT NULL default '0',
 grant_create smallint NOT NULL default '0',
 PRIMARY KEY  (vid,rid)
); 

