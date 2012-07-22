/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 2; tab-width: 2 -*- */
/*!
* main.cc
* Copyright (C) Diego Rubin 2011 <rubin.diego@gmail.com>
*
* Gnomato is free software: you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Gnomato is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*
* Author: Diego Rubin <rubin.diego@gmail.com>
*
*/

#include <gtkmm.h>
#include <giomm.h>
#include <glibmm.h>

#include <glibmm/i18n.h>
#include <libnotify/notify.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#include <iostream>

#include "sqls.h"
#include "utils.h"
#include "win_main.h"
#include "database.h"
#include "dbus_client.h"


sqlite3 *db;
char dbpath[1000];
WinMain* winMain;

static Glib::RefPtr<Gio::DBus::NodeInfo> introspection_data;

static Glib::ustring introspection_xml =
  "<node>"
  "  <interface name='com.diegorubin.Gnomato'>"
  "    <method name='GetCurrentTime'>"
  "      <arg type='s' name='iso8601' direction='out'/>"
  "    </method>"
  "  </interface>"
  "</node>";

guint registered_id = 0;

static void on_method_call(const Glib::RefPtr<Gio::DBus::Connection>& /* connection */,
  const Glib::ustring& /* sender */, const Glib::ustring& /* object_path */,
  const Glib::ustring& /* interface_name */, const Glib::ustring& method_name,
  const Glib::VariantContainerBase& parameters,
  const Glib::RefPtr<Gio::DBus::MethodInvocation>& invocation)
{
  if(method_name == "GetElapsedTime")
  {
    const Glib::Variant<Glib::ustring> time_var =
      Glib::Variant<Glib::ustring>::create(winMain->get_current_time());

    Glib::VariantContainerBase response =
      Glib::VariantContainerBase::create_tuple(time_var);

    // Return the tuple with the included time.
    invocation->return_value(response);
  }
  else
  {
    // Non-existent method on the interface.
    Gio::DBus::Error error(Gio::DBus::Error::UNKNOWN_METHOD,
      "Method does not exist.");
    invocation->return_error(error);
  }
}

//This must be a global instance. See the InterfaceVTable documentation.
//TODO: Make that unnecessary.
const Gio::DBus::InterfaceVTable interface_vtable(sigc::ptr_fun(&on_method_call));

void on_bus_acquired(const Glib::RefPtr<Gio::DBus::Connection>& connection, const Glib::ustring& /* name */)
{
  // Export an object to the bus:

  // See https://bugzilla.gnome.org/show_bug.cgi?id=646417 about avoiding
  // the repetition of the interface name:
  try
  {
    registered_id = connection->register_object("/org/glibmm/DBus/TestObject",
      introspection_data->lookup_interface(),
      interface_vtable);
  }
  catch(const Glib::Error& ex)
  {
    std::cerr << "Registration of object failed." << std::endl;
  }

  return;
}

void on_name_acquired(const Glib::RefPtr<Gio::DBus::Connection>& /* connection */, const Glib::ustring& /* name */)
{
  //TODO: What is this good for? See https://bugzilla.gnome.org/show_bug.cgi?id=646427
}

void on_name_lost(const Glib::RefPtr<Gio::DBus::Connection>& connection, const Glib::ustring& /* name */)
{
  connection->unregister_object(registered_id);
}

void bootstrap()
{
  std::ifstream *fhome = new std::ifstream(home());

  // diretorio home nao existe
  if(!*fhome){
    mkdir(home(),0700);   
  }

  fhome = new std::ifstream(dbfile());
  
  // banco nao exite
  if(!*fhome){
   
    std::cout << _("Creating database") << std::endl;

    int result = sqlite3_open(dbpath, &db);

    int rc;
    char *cError;

    rc = sqlite3_exec(db,CREATE_TASK,NULL,NULL,&cError);
    if(rc != SQLITE_OK){
      cerr << "Sqlite3:" << cError << std::endl;
    }
    else{
      std::cout << _("Database successfully created ") << std::endl;
    }

    sqlite3_close(db);

  }

  delete(fhome);
  
}

int main(int argc, char **argv)
{

  sprintf(dbpath, "%s", dbfile());
  bootstrap();

  //intltool
  bindtextdomain(GETTEXT_PACKAGE, GNOMATO_LOCALEDIR);
  bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
  textdomain(GETTEXT_PACKAGE);
  
  // connect to database
  int result = sqlite3_open(dbpath, &db);
  if (result) {
      cerr << _("Could not open the database. Error: ") << sqlite3_errmsg(db);
      exit(1);
  }

  Gtk::Main kit(argc, argv);


  if(argc > 1) // command
  {

  }
  else // gui
  {

    notify_init("Gnomato");

    Glib::RefPtr<Gtk::Builder> refBuilder = Gtk::Builder::create();
    try
    {
      refBuilder->add_from_file(GNOMATO_DATADIR "/gnomato.ui");
    }
    catch(const Glib::FileError& ex)
    {
      std::cerr << "FileError: " << ex.what() << std::endl;
      return 1;
    }
    catch(const Gtk::BuilderError& ex)
    {
      std::cerr << "BuilderError: " << ex.what() << std::endl;
      return 1;
    }

    Glib::RefPtr<Gtk::StatusIcon> systray = 
      Gtk::StatusIcon::create_from_file(GNOMATO_DATADIR "/tomato.png");

    winMain = 0;
    refBuilder->get_widget_derived("winMain", winMain);

    Gio::init();

    try
    {
      introspection_data = Gio::DBus::NodeInfo::create_for_xml(introspection_xml);
    }
    catch(const Glib::Error& ex)
    {
      std::cerr << "Unable to create introspection data: " << ex.what() <<
        "." << std::endl;
      return 1;
    }

    const guint id = Gio::DBus::own_name(Gio::DBus::BUS_TYPE_SESSION,
    "com.diegorubin.Gnomato",
    sigc::ptr_fun(&on_bus_acquired),
    sigc::ptr_fun(&on_name_acquired),
    sigc::ptr_fun(&on_name_lost));

    if(winMain)
    {
      winMain->set_icon_from_file(GNOMATO_DATADIR "/tomato.png");
      winMain->set_systray(systray);
      winMain->show();
      kit.run();
    }

    Gio::DBus::unown_name(id);
    delete winMain;
    sqlite3_close(db);
  }

  return 0;
}

