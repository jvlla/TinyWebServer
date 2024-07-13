drop database if exists webServer;
create database webServer;
use webServer

drop table if exists image;
create table image (
    image_id bigint auto_increment primary key,
    image_ip int unsigned,
    image_name varchar(1000)
);

insert into image(image_ip, image_name) values(16777343, "test image 1.jpg");
insert into image(image_ip, image_name) values(16777343, "test image 2.jpg");
insert into image(image_ip, image_name) values(16777343, "test image 3.jpg");
insert into image(image_ip, image_name) values(16777343, "test image 4.jpg");
select * from image;
