drop database if exists webServer;
create database webServer;
use webServer

drop table if exists image;
create table image (
    image_id bigint auto_increment primary key,
    image_ip int unsigned,
    image_name varchar(1000)
);

insert into image(image_ip, image_name) values(16777343, "测试图片1.jpg");
insert into image(image_ip, image_name) values(16777343, "测试图片2.jpg");
insert into image(image_ip, image_name) values(16777343, "测试图片3.jpg");
insert into image(image_ip, image_name) values(16777343, "测试图片4.jpg");
select * from image;

-- root的密码是jvlla，编译加-lmysqlclient