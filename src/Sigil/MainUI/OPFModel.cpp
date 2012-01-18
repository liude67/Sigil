/************************************************************************
**
**  Copyright (C) 2009, 2010, 2011  Strahinja Markovic  <strahinja.markovic@gmail.com>
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  Sigil is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Sigil.  If not, see <http://www.gnu.org/licenses/>.
**
*************************************************************************/

#include <stdafx.h>
#include "OPFModel.h"
#include "BookManipulation/Book.h"
#include "ResourceObjects/Resource.h"
#include "ResourceObjects/HTMLResource.h"
#include "ResourceObjects/OPFResource.h"
#include "ResourceObjects/NCXResource.h"
#include "SourceUpdates/UniversalUpdates.h"
#include "BookManipulation/FolderKeeper.h"
#include "Misc/Utility.h"
#include <limits>

static const int NO_READING_ORDER   = std::numeric_limits< int >::max();
static const int READING_ORDER_ROLE = Qt::UserRole + 2;
static const QList< QChar > FORBIDDEN_FILENAME_CHARS = QList< QChar >() << '<' << '>' << ':' 
                                                                        << '"' << '/' << '\\'
                                                                        << '|' << '?' << '*';

OPFModel::OPFModel( QWidget *parent )
    : 
    QStandardItemModel( parent ),
    m_RefreshInProgress( false ),
    m_Book( NULL ),
      m_TextFolderItem(   *new QStandardItem( tr("Text")  ) ),
      m_StylesFolderItem( *new QStandardItem( tr("Styles") ) ),
      m_ImagesFolderItem( *new QStandardItem( tr("Images") ) ),
      m_FontsFolderItem(  *new QStandardItem( tr("Fonts")  ) ),
      m_MiscFolderItem(   *new QStandardItem( tr("Misc")  ) )
{
    connect( this, SIGNAL( rowsRemoved(        const QModelIndex&, int, int ) ),
             this, SLOT(   RowsRemovedHandler( const QModelIndex&, int, int ) ) );  

    connect( this, SIGNAL( itemChanged(        QStandardItem* ) ),
             this, SLOT(   ItemChangedHandler( QStandardItem* ) ) );   

    QList< QStandardItem* > items;

    items.append( &m_TextFolderItem   );
    items.append( &m_StylesFolderItem );
    items.append( &m_ImagesFolderItem );
    items.append( &m_FontsFolderItem  );
    items.append( &m_MiscFolderItem   );

    QIcon folder_icon = QFileIconProvider().icon( QFileIconProvider::Folder );

    foreach( QStandardItem *item, items )
    {
        item->setIcon( folder_icon );
        item->setEditable( false );
        item->setDragEnabled( false );
        item->setDropEnabled( false );
        appendRow( item );
    }    

    // We enable reordering of files in the text folder
    m_TextFolderItem.setDropEnabled( true );
    invisibleRootItem()->setDropEnabled( false );
}


void OPFModel::SetBook( QSharedPointer< Book > book )
{
    m_Book = book;

    connect( this, SIGNAL( BookContentModified() ), m_Book.data(), SLOT( SetModified() ) );

    Refresh();
}


void OPFModel::Refresh()
{
    m_RefreshInProgress = true;

    InitializeModel();

    SortFilesByFilenames();
    SortHTMLFilesByReadingOrder();

    m_RefreshInProgress = false;
}


QModelIndex OPFModel::GetFirstHTMLModelIndex()
{
    if ( !m_TextFolderItem.hasChildren() )

        boost_throw( NoHTMLFiles() );

    return m_TextFolderItem.child( 0 )->index();
}


QModelIndex OPFModel::GetTextFolderModelIndex()
{
    return m_TextFolderItem.index();
}


// Get the index of the given resource regardless of folder
QModelIndex OPFModel::GetModelItemIndex( Resource &resource, IndexChoice indexChoice )
{
    Resource::ResourceType resourceType = resource.Type();
    QStandardItem *folder = NULL;

    if (resourceType == Resource::OPFResourceType || resourceType == Resource::NCXResourceType )
    {
            folder = invisibleRootItem();
    }
    else
    {
        for ( int i = 0; i < invisibleRootItem()->rowCount() && folder == NULL; ++i )
        {
            QStandardItem *child = invisibleRootItem()->child( i );
    
            if ( (child == &m_TextFolderItem && resourceType == Resource::HTMLResourceType ) ||
                 (child == &m_ImagesFolderItem && resourceType == Resource::ImageResourceType ) ||
                 (child == &m_StylesFolderItem && 
                    ( resourceType == Resource::CSSResourceType || resourceType == Resource::XPGTResourceType ) ) ||
                 (child == &m_FontsFolderItem && resourceType == Resource::FontResourceType ) ||
                 (child == &m_MiscFolderItem && resourceType == Resource::GenericResourceType ))
            {
                folder = child;
            }
        }
    }
    return GetModelFolderItemIndex( folder, resource, indexChoice );
}


// Get the index of the given resource in a specific folder 
QModelIndex OPFModel::GetModelFolderItemIndex( QStandardItem const *folder, Resource &resource, IndexChoice indexChoice )
{
    if ( folder != NULL )
    {
        int rowCount = folder->rowCount();
        for ( int i = 0; i < rowCount ; ++i )
        {
            QStandardItem *item = folder->child( i );
            const QString &identifier = item->data().toString();
    
            if ( !identifier.isEmpty() && identifier == resource.GetIdentifier() )
            {
                if ( folder != invisibleRootItem() )
                {
                    if ( indexChoice == IndexChoice_Previous && i > 0 )
                    {
                        i--;
                    }
                    else if ( indexChoice == IndexChoice_Next && ( i + 1 < rowCount ) )
                    {
                        i++;
                    }
                }
                return index( i, 0, folder->index() );
            }
        }
    }
    return index( 0, 0 );
}


Resource::ResourceType OPFModel::GetResourceType( QStandardItem const *item )
{
    Q_ASSERT( item );

    if ( item == &m_TextFolderItem )

        return Resource::HTMLResourceType;

    if ( item == &m_StylesFolderItem )

        return Resource::CSSResourceType;

    if ( item == &m_ImagesFolderItem )

        return Resource::ImageResourceType;

    if ( item == &m_FontsFolderItem )

        return Resource::FontResourceType;

    if ( item == &m_MiscFolderItem )

        return Resource::GenericResourceType;  
    
    const QString &identifier = item->data().toString();
    return m_Book->GetFolderKeeper().GetResourceByIdentifier( identifier ).Type();
}


void OPFModel::sort( int column, Qt::SortOrder order )
{
    return;
}


Qt::DropActions OPFModel::supportedDropActions() const
{
    return Qt::MoveAction;
}


//   This function initiates HTML reading order updating when the user
// moves the HTML files in the Book Browser.
//   You would expect the use of QAbstractItemModel::rowsMoved, but that
// signal is never emitted because in QStandardItemModel, row moves
// are actually handled by creating a copy of the row in the new position
// and then deleting the old row. 
//   Yes, it's stupid and violates the guarantees of the QAbstractItemModel
// class. Oh and it's not documented anywhere. Nokia FTW.
//   This also handles actual HTML item deletion.
void OPFModel::RowsRemovedHandler( const QModelIndex &parent, int start, int end )
{
    if ( m_RefreshInProgress || 
         itemFromIndex( parent ) != &m_TextFolderItem )
    {
        return;
    }

    UpdateHTMLReadingOrders();
}


void OPFModel::ItemChangedHandler( QStandardItem *item )
{
    Q_ASSERT( item );

    const QString &identifier = item->data().toString(); 

    if ( identifier.isEmpty() )

        return;

    Resource *resource = &m_Book->GetFolderKeeper().GetResourceByIdentifier( identifier );
    Q_ASSERT( resource );

    const QString &old_fullpath = resource->GetFullPath();
    const QString &old_filename = resource->Filename();
    const QString &new_filename = item->text();
    
    if ( old_filename == new_filename || 
         !FilenameIsValid( old_filename, new_filename )   )
    {
        item->setText( old_filename );
        return;
    }

    bool rename_sucess = resource->RenameTo( new_filename );

    if ( !rename_sucess )
    {
        Utility::DisplayStdErrorDialog( 
            tr( "The file could not be renamed." )
            );

        item->setText( old_filename );
        return;
    }

    QHash< QString, QString > update;
    update[ old_fullpath ] = "../" + resource->GetRelativePathToOEBPS();

    QApplication::setOverrideCursor( Qt::WaitCursor );
    UniversalUpdates::PerformUniversalUpdates( true, m_Book->GetFolderKeeper().GetResourceList(), update );
    QApplication::restoreOverrideCursor();

    emit BookContentModified();
}


void OPFModel::InitializeModel()
{
    Q_ASSERT( m_Book );

    ClearModel();

    QList< Resource* > resources = m_Book->GetFolderKeeper().GetResourceList();

    foreach( Resource *resource, resources )
    {
        QStandardItem *item = new QStandardItem( resource->Icon(), resource->Filename() );
        item->setDropEnabled( false );
        item->setData( resource->GetIdentifier() );
        
        if ( resource->Type() == Resource::HTMLResourceType )
        {
            int reading_order = 
                m_Book->GetOPF().GetReadingOrder( *qobject_cast< HTMLResource* >( resource ) );

            if ( reading_order == -1 )
            
                reading_order = NO_READING_ORDER;

            item->setData( reading_order, READING_ORDER_ROLE );
            m_TextFolderItem.appendRow( item );
        }

        else if ( resource->Type() == Resource::CSSResourceType || 
                  resource->Type() == Resource::XPGTResourceType 
                )
        {
            item->setDragEnabled(false);
            m_StylesFolderItem.appendRow( item );
        }

        else if ( resource->Type() == Resource::ImageResourceType )
        {
            m_ImagesFolderItem.appendRow( item );
        }

        else if ( resource->Type() == Resource::FontResourceType )
        {
            item->setDragEnabled(false);
            m_FontsFolderItem.appendRow( item );
        }

        else if ( resource->Type() == Resource::OPFResourceType || 
                  resource->Type() == Resource::NCXResourceType )
        {
            item->setEditable( false );
            item->setDragEnabled(false);
            appendRow( item );
        }

        else
        {
            m_MiscFolderItem.appendRow( item );        
        }
    }           
}


void OPFModel::UpdateHTMLReadingOrders()
{
    QList< HTMLResource* > reading_order_htmls;

    for ( int i = 0; i < m_TextFolderItem.rowCount(); ++i )
    {
        QStandardItem *html_item = m_TextFolderItem.child( i );

        Q_ASSERT( html_item );

        html_item->setData( i, READING_ORDER_ROLE );
        HTMLResource *html_resource =  qobject_cast< HTMLResource* >(
            &m_Book->GetFolderKeeper().GetResourceByIdentifier( html_item->data().toString() ) );

        if ( html_resource != NULL )
                
            reading_order_htmls.append( html_resource );        
    }

    m_Book->GetOPF().UpdateSpineOrder( reading_order_htmls );
    m_Book->SetModified();
}


void OPFModel::ClearModel()
{
    while ( m_TextFolderItem.rowCount() != 0 )
    {
        m_TextFolderItem.removeRow( 0 );
    }

    while ( m_StylesFolderItem.rowCount() != 0 )
    {
        m_StylesFolderItem.removeRow( 0 );
    }

    while ( m_ImagesFolderItem.rowCount() != 0 )
    {
        m_ImagesFolderItem.removeRow( 0 );
    }

    while ( m_FontsFolderItem.rowCount() != 0 )
    {
        m_FontsFolderItem.removeRow( 0 );
    }

    while ( m_MiscFolderItem.rowCount() != 0 )
    {
        m_MiscFolderItem.removeRow( 0 );
    }

    int i = 0; 
    while ( i < invisibleRootItem()->rowCount() )
    {
        QStandardItem *child = invisibleRootItem()->child( i, 0 );

        if ( child != &m_TextFolderItem   &&
             child != &m_StylesFolderItem &&
             child != &m_ImagesFolderItem &&
             child != &m_FontsFolderItem  &&
             child != &m_MiscFolderItem )
        {
            invisibleRootItem()->removeRow( i );
        }

        else
        {
            ++i;
        }
    }
}


void OPFModel::SortFilesByFilenames()
{
    for ( int i = 0; i < invisibleRootItem()->rowCount(); ++i )
    {
        invisibleRootItem()->child( i )->sortChildren( 0 );
    }
}


void OPFModel::SortHTMLFilesByReadingOrder()
{
    int old_sort_role = sortRole();
    setSortRole( READING_ORDER_ROLE );

    m_TextFolderItem.sortChildren( 0 );

    setSortRole( old_sort_role );
}


bool OPFModel::FilenameIsValid( const QString &old_filename, const QString &new_filename )
{
    if ( new_filename.isEmpty() )
    {
        Utility::DisplayStdErrorDialog( 
            tr( "The filename cannot be empty." )
            );

        return false;
    }

    foreach( QChar character, new_filename )
    {
        if ( FORBIDDEN_FILENAME_CHARS.contains( character ) )
        {
            Utility::DisplayStdErrorDialog( 
                tr( "A filename cannot contains the character \"%1\"." )
                .arg( character )
                );

            return false;
        }
    }

    const QString &old_extension = QFileInfo( old_filename ).suffix();
    const QString &new_extension = QFileInfo( new_filename ).suffix();

    // We normally don't allow an extension change, but we
    // allow it for changes within the following sets:
    // HTML, HTM, XHTML and XML.
    // JPG, JPEG.
    // TIF, TIFF.
    if ( old_extension != new_extension &&
         !(
             ( TEXT_EXTENSIONS.contains( old_extension ) && TEXT_EXTENSIONS.contains( new_extension ) ) ||
             ( JPG_EXTENSIONS.contains( old_extension ) && JPG_EXTENSIONS.contains( new_extension ) ) ||
             ( TIFF_EXTENSIONS.contains( old_extension ) && TIFF_EXTENSIONS.contains( new_extension ) )
         )
       )
    {
        Utility::DisplayStdErrorDialog( 
            tr( "This file's extension cannot be changed in that way.\n"
                "You used \"%1\", and the old extension was \"%2\"." )
            .arg( new_extension )
            .arg( old_extension )
            );

        return false;
    }
    
    if ( new_filename != m_Book->GetFolderKeeper().GetUniqueFilenameVersion( new_filename ) )
    {
        Utility::DisplayStdErrorDialog( 
            tr( "The filename \"%1\" is already in use.\n" )
            .arg( new_filename )
            );

        return false;
    }

    return true;
}


