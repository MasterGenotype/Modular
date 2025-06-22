# This script scrapes a user's download history from Nexus Mods.
# It automates a Firefox browser in headless mode to log in via cookies,
# navigate through the download history pages, and recursively scrape details
# for each mod and its required mods. The final data is saved as a JSON file.

import json
import time
import sys # For command line arguments and stderr
from pathlib import Path
from selenium import webdriver
from selenium.common.exceptions import NoSuchElementException, TimeoutException, WebDriverException
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.firefox.options import Options

def configure_driver():
    """
    Configures and initializes a headless Firefox WebDriver instance.
    
    Returns:
        webdriver.Firefox: The configured Selenium WebDriver instance.
    """
    options = Options()
    options.add_argument("--headless") # Run in headless mode for background execution
    options.add_argument("--log-level=3")
    options.add_argument("--mute-audio")
    options.add_argument("--no-sandbox")
    options.add_argument("--disable-dev-shm-usage")
    options.set_preference("http.response.timeout", 1)
    options.set_preference('permissions.default.stylesheet', 2)
    options.set_preference('permissions.default.image', 2)
    options.set_preference('dom.ipc.plugins.enabled.libflashplayer.so', 'false')
    options.set_capability('pageLoadStrategy', 'eager')
    return webdriver.Firefox(options=options)

def scrape_files(driver, wait, mod_url):
    """
    Scrapes a single mod page for its files and their requirements.

    This function navigates to the 'files' tab of a mod page, extracts download links for each main file,
    and recursively scrapes the requirements for each of those files. It includes robust error handling
    for network issues and missing elements.

    Args:
        driver (webdriver.Firefox): The Selenium WebDriver instance.
        wait (WebDriverWait): The Selenium WebDriverWait instance.
        mod_url (str): The URL of the mod page to scrape.

    Returns:
        dict: A dictionary containing the mod's URL and a nested dictionary of its files.
    """
    result = {
        'url': mod_url,
        'files': {}
    }
    try:
        driver.get(mod_url + '?tab=files')
        wait.until(EC.visibility_of_element_located((By.XPATH, '//dt[contains(@id, "file-expander-header")]')))
    except (TimeoutException, WebDriverException) as e:
        sys.stderr.write(f"Error navigating to mod files page {mod_url}: {e}\n")
        return result # Return empty result for this mod

    filelinks = dict()
    # Find all file headers on the page.
    file_headers = driver.find_elements(by='xpath', value='//dt[contains(@id, "file-expander-header")]')
    for i, e in enumerate(file_headers):
        try:
            # Check for the expander icon; if it's not there, it's likely not a main file entry.
            e.find_element(by='xpath', value='./div/i')
        except NoSuchElementException:
            continue
        
        name = e.get_attribute('data-name')
        link = None # Initialize link to avoid UnboundLocalError
        try:
            # Try to find the link directly if the file details are already expanded.
            # The XPath uses `following-sibling` for a more robust selection.
            link_element = e.find_element('xpath', './following-sibling::dd[1]//a[contains(@class, "btn inline-flex")]')
            link = link_element.get_attribute('href').replace('&nmm=1', '')
        except NoSuchElementException:
            # If not found, click the file header to expand it.
            try:
                e.click()
                # Wait for the details section to become visible after clicking.
                wait.until(EC.visibility_of_element_located((By.XPATH, f'./following-sibling::dd[1][contains(@class, "open")]')))
                link_element = e.find_element('xpath', './following-sibling::dd[1]//a[contains(@class, "btn inline-flex")]')
                link = link_element.get_attribute('href').replace('&nmm=1', '')
            except (NoSuchElementException, TimeoutException) as inner_e:
                sys.stderr.write(f"Warning: Could not get download link for file '{name}' on mod {mod_url}: {inner_e}\n")
                # Continue to the next file header if this one fails.
                continue
        
        # If a link was successfully found, add it to our dictionary.
        if link:
            filelinks[name] = link
    
    # For each file found, visit its download page to get requirements and the real filename.
    for name, link in filelinks.items():
        requirements = None
        try:
            driver.get(link)
            # Check if the download is blocked by a requirements pop-up.
            if 'ModRequirementsPopUp' in link: # This check is based on the URL, which might not always be reliable
                try:
                    # Wait for the requirements pop-up to be visible.
                    wait.until(EC.visibility_of_element_located((By.ID, 'ModRequirementsPopUp')))
                    required_elements = driver.find_elements(by='xpath', value='//div[@class="mod-requirements-tab-content"]/ul/li/a')
                    required = {e.find_element(by='xpath', value='./span').text: e.get_attribute('href') for e in required_elements}
                    
                    # Find the button that leads to the actual download page.
                    modlink_element = driver.find_element('xpath', '//div[@class="mod-requirements-tab-content"]/a[@class="btn"]')
                    modlink = modlink_element.get_attribute('href')

                    if required:
                        # Recursively scrape the requirements.
                        requirements = {k: scrape_files(driver, wait, v) for k, v in required.items() if v.startswith('https://www.nexusmods.com/')}
                        if not requirements:
                            requirements = None # Set to None if no valid requirements were found after recursion
                    
                    # Navigate to the actual download page after handling requirements.
                    driver.get(modlink)
                except (NoSuchElementException, TimeoutException) as req_e:
                    sys.stderr.write(f"Warning: Could not process requirements popup for {name} on {mod_url}: {req_e}\n")
            
            # Get the filename from the download page header.
            file_element = wait.until(EC.visibility_of_element_located((By.XPATH, '//div[@class="header"]')))
            file = file_element.text.splitlines()[0].strip()
            result['files'][name] = {'requirements': requirements, 'filename': file}

        except (TimeoutException, NoSuchElementException, WebDriverException) as e:
            # Handle errors during individual file processing.
            sys.stderr.write(f"Error processing file '{name}' from mod {mod_url}: {e}\n")
            result['files'][name] = {'requirements': requirements, 'filename': 'Error: Could not retrieve filename'}
            continue # Continue to next file even if one fails
    
    return result

def main(cookie_path, output_path):
    """
    The main function to orchestrate the scraping process.

    Args:
        cookie_path (str): The file path to the cookies.json file.
        output_path (str): The file path where the final JSON output will be saved.
    """
    Firefox = None
    try:
        Firefox = configure_driver()
        wait = WebDriverWait(Firefox, 5) # Increased wait time slightly for more reliability

        # Login using cookies
        Firefox.get('https://www.nexusmods.com/')
        try:
            cookies_data = json.loads(Path(cookie_path).read_text())
            for i in cookies_data:
                if 'name' in i and 'value' in i:
                    Firefox.add_cookie({'name': i['name'], 'value': i['value']})
                else:
                    sys.stderr.write(f"Warning: Malformed cookie entry: {i}\n")
            Firefox.get('https://www.nexusmods.com/') # Refresh after adding cookies
            time.sleep(2) # Give a moment for cookies to apply and for the page to reflect the logged-in state.
        except (FileNotFoundError, json.JSONDecodeError) as e:
            sys.stderr.write(f"Error loading cookies from {cookie_path}: {e}. Please ensure the file exists and is valid JSON.\n")
            return # Exit if cookies cannot be loaded

        # Navigate to download history
        try:
            Firefox.get('https://www.nexusmods.com/users/myaccount?tab=download+history')
            wait.until(EC.visibility_of_element_located((By.XPATH, "//div[@class='tracking-title']/a")))
        except (TimeoutException, WebDriverException) as e:
            sys.stderr.write(f"Error navigating to download history: {e}\n")
            return

        # Scrape all mod links from the download history, paginating through all pages.
        mods = {}
        while True:
            try:
                # Wait for the list of mods on the current page to be present.
                wait.until(EC.presence_of_all_elements_located((By.XPATH, "//div[@class='tracking-title']/a")))
                for mod in Firefox.find_elements(by='xpath', value="//div[@class='tracking-title']/a"):
                    name = mod.text.strip()
                    url = mod.get_attribute('href')
                    if name and url:
                        mods[name] = url
                
                # Find the 'next' button and check if it's disabled.
                next_button = wait.until(EC.element_to_be_clickable((By.XPATH, "//div/a[contains(@class, 'paginate_button next')]")))
                if 'disabled' in next_button.get_attribute('class'):
                    break # Exit loop if on the last page
                else:
                    next_button.click()
                    # This is a crucial wait. It waits for the old button to become stale (i.e., detached from the DOM),
                    # which confirms that the page has started to update/navigate.
                    wait.until(EC.staleness_of(next_button)) # Wait for the old button to disappear/become stale
                    wait.until(EC.visibility_of_element_located((By.XPATH, "//div[@class='tracking-title']/a"))) # Wait for new content to load
            except (NoSuchElementException, TimeoutException, WebDriverException) as e:
                sys.stderr.write(f"Error during pagination of download history: {e}\n")
                break # Break loop on pagination error

        modfiles = {}
        for name, url in mods.items():
            try:
                game = url.split('/')[3]
                if game not in modfiles:
                    modfiles[game] = {}
                modfiles[game][name] = scrape_files(Firefox, wait, url)
            except IndexError:
                sys.stderr.write(f"Warning: Could not extract game from URL: {url}\n")
            except Exception as e:
                sys.stderr.write(f"Error processing mod {name} ({url}): {e}\n")

        # Save all collected data to a JSON file.
        try:
            Path(output_path).write_text(json.dumps(modfiles, ensure_ascii=False, indent=4), encoding='utf8')
        except Exception as e:
            sys.stderr.write(f"Error writing output JSON to {output_path}: {e}\n")

    except Exception as e:
        sys.stderr.write(f"An unhandled error occurred in the main scraper process: {e}\n")
    finally:
        # Ensure the browser is always closed, even if errors occur.
        if Firefox:
            Firefox.quit() # Ensure browser is closed

if __name__ == "__main__":
    # This block allows the script to be run from the command line.
    # It handles command-line arguments for cookie and output file paths.
    
    # Default paths, can be overridden by command line arguments
    default_cookie_path = 'D:/cookies.json'
    default_output_path = 'D:/nexusmods_downloaded.json'

    cookie_path = sys.argv[1] if len(sys.argv) > 1 else default_cookie_path
    output_path = sys.argv[2] if len(sys.argv) > 2 else default_output_path

    main(cookie_path, output_path)